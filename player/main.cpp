#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <MinimalSocket/udp/UdpSocket.h>
#include <chrono>
#include <thread>

// ---------- ESTRUCTURAS DE DATOS ----------

// Representa el lado del campo donde juega el equipo
enum class Side
{
    Left, Right, Unknown 
};

std::ostream& operator<<(std::ostream& os, Side s)
{
    switch (s) {
        case Side::Left:    os << "Left"; break;
        case Side::Right:   os << "Right"; break;
        default:            os << "Unknown"; break;
    }
    return os;
}

// Coordenadas 2D en el campo
struct Point
{
    double x{0.0};
    double y{0.0};
};

std::ostream& operator<<(std::ostream& os, const Point& p)
{
    os << "Point(x=" << p.x << ", y=" << p.y << ")";
    return os;
}

// Información sobre un objeto visto: distancia, dirección y visibilidad
struct ObjectInfo
{
    double dist{0.0};     // Distancia al objeto
    double dir{0.0};      // Dirección en grados
    bool visible{false};  // Si el objeto es visible
};

std::ostream& operator<<(std::ostream& os, const ObjectInfo& o)
{
    os << "ObjectInfo(dist=" << o.dist
       << ", dir=" << o.dir
       << ", visible=" << std::boolalpha << o.visible << ")";
    return os;
}

// Información visual del jugador en un instante dado
struct SeeInfo
{
    int time{0};              // Tiempo de simulación
    ObjectInfo ball{};        // Información del balón
    ObjectInfo own_goal{};    // Información de portería propia
    ObjectInfo opp_goal{};    // Información de portería rival
};

std::ostream& operator<<(std::ostream& os, const SeeInfo& s)
{
    os << "SeeInfo(time=" << s.time
       << ", ball=" << s.ball
       << ", own_goal=" << s.own_goal
       << ", opp_goal=" << s.opp_goal
       << ")";
    return os;
}

// Información sensorial interna del jugador
struct SenseInfo 
{
    int time{0};              // Tiempo de simulación
    double speed{0.0};        // Velocidad actual
    double stamina{0.0};      // Resistencia actual
    double headAngle{0.0};    // Ángulo de la cabeza
};

std::ostream& operator<<(std::ostream& os, const SenseInfo& s)
{
    os << "SenseInfo(time=" << s.time
       << ", speed=" << s.speed
       << ", stamina=" << s.stamina
       << ", headAngle=" << s.headAngle
       << ")";
    return os;
}

// Información completa del jugador
struct PlayerInfo 
{
    std::string team{""};
    Side side{Side::Unknown};
    int number{-1};
    std::string playMode{""};
    SeeInfo see{};
    SenseInfo sense{};
    Point initialPosition{};  // Posición inicial asignada según el dorsal
};

std::ostream& operator<<(std::ostream &os, const PlayerInfo &player) 
{
    os << "Player created (side: " << player.side
       << ", number: " << player.number
       << ", playmode: " << player.playMode
       << ", position: (" << player.initialPosition.x << ", " << player.initialPosition.y << "))";
    return os;
}

// ---------- FUNCIONES AUXILIARES ----------

// Calcula la posición inicial en el campo según el número de dorsal (1-11)
Point calcKickOffPosition(int unum)
{
    static const Point positions[11] = {
        { -50.0,   0.0 },                                                       // 1: Portero
        { -40.0, -15.0 }, { -42.0,  -5.0 }, { -42.0,   5.0 }, { -40.0,  15.0 }, // 2-5: Defensa
        { -28.0, -10.0 }, { -22.0,   0.0 }, { -28.0,  10.0 },                   // 6-8: Medios
        { -15.0, -15.0 }, { -12.0,   0.0 }, { -15.0,  15.0 }                    // 9-11: Delanteros
    };

    if (unum < 1 || unum > 11) {
        return Point{0.0, 0.0};
    }

    return positions[unum - 1];
}

std::string receiveMsgFromServer(MinimalSocket::udp::Udp<true> &udp_socket, std::size_t message_max_size)
{
    auto received_message = udp_socket.receive(message_max_size);

    if (!received_message) {
        std::cerr << "Error receiving message from server" << std::endl;
        return "";
    }

    std::string received_message_content = received_message->received_message;
    std::cout << "Received message: " << received_message_content << std::endl;

    return received_message_content;
}

// ---------- HELPERS DE PARSING ----------

// Elimina espacios y paréntesis al inicio del string_view
void skipDelims(std::string_view& sv)
{
    const char* delims = " ()";
    size_t pos = sv.find_first_not_of(delims);
    if (pos == std::string_view::npos) {
        sv = {};
    } else {
        sv.remove_prefix(pos);
    }
}

// Extrae el siguiente token del string_view (palabra delimitada por espacios o paréntesis)
std::string_view nextToken(std::string_view& sv)
{
    skipDelims(sv);
    if (sv.empty()) return {};

    size_t end = sv.find_first_of(" ()");
    std::string_view tok = sv.substr(0, end);
    if (end == std::string_view::npos) {
        sv = {};
    } else {
        sv.remove_prefix(end);
    }
    return tok;
}

// Busca y parsea la información de un objeto específico en el mensaje see
// Retorna true si el objeto fue encontrado y parseado correctamente
bool parseObjectInfo(std::string_view msg,
                      std::string_view objectTag,
                      ObjectInfo& out)
{
    size_t pos = msg.find(objectTag);
    if (pos == std::string_view::npos) {
        out = ObjectInfo{};
        return false;
    }

    std::string_view sv = msg.substr(pos + objectTag.size());

    // Extraer distancia y dirección
    auto distTok = nextToken(sv);
    auto dirTok  = nextToken(sv);

    if (distTok.empty() || dirTok.empty()) {
        out.visible = false;
        return false;
    }

    out.dist = std::stod(std::string(distTok));
    out.dir  = std::stod(std::string(dirTok));
    out.visible = true;
    return true;
}

// ---------- FUNCIONES DE PARSING ----------

// Parsea el mensaje de inicialización del servidor
// Ejemplo: (init l 1 before_kick_off) -> lado izquierdo, dorsal 1
void parseInitMsg(const std::string &msg, PlayerInfo &player)
{
    std::string_view sv = msg;

    auto cmdTok = nextToken(sv);

    auto sideTok = nextToken(sv);
    if (sideTok == "l")
        player.side = Side::Left;
    else if (sideTok == "r")
        player.side = Side::Right;
    else
        player.side = Side::Unknown;

    auto numberTok = nextToken(sv);
    player.number = std::stoi(std::string(numberTok));

    auto playmodeTok = nextToken(sv); 
    player.playMode = std::string(playmodeTok);

    auto position = calcKickOffPosition(player.number);
    player.initialPosition = position;
}

// Parsea el mensaje de visión del servidor
// Ejemplo: (see 0 ... ((g r) 102.5 0) ... ((b) 49.4 0) ...)
void parseSeeMsg(const std::string &msg, PlayerInfo &player)
{
    std::string_view sv = msg;

    auto cmdTok  = nextToken(sv);
    auto timeTok = nextToken(sv);
    player.see.time = std::stoi(std::string(timeTok));

    parseObjectInfo(msg, "(b)", player.see.ball);

    // Identificar porterías según el lado del jugador
    if (player.side == Side::Left) {
        // Nuestro lado es el izquierdo -> nuestra portería es g l, rival g r
        parseObjectInfo(msg, "(g l)", player.see.own_goal);
        parseObjectInfo(msg, "(g r)", player.see.opp_goal);
    } else if (player.side == Side::Right) {
        // Nuestro lado es el derecho -> nuestra portería es g r, rival g l
        parseObjectInfo(msg, "(g r)", player.see.own_goal);
        parseObjectInfo(msg, "(g l)", player.see.opp_goal);
    } 
}

// Parsea el mensaje de información sensorial interna del jugador
// Ejemplo: (sense_body 0 ... (stamina 8000 1 130600) (speed 0 0) (head_angle 0) ...)
void parseSenseMsg(const std::string &msg, PlayerInfo &player)
{
    // TODO: Implementar parsing completo de sense_body
}

// ---------- FUNCIONES PARA ENVIAR COMANDOS ----------

// Envía el comando de inicialización al servidor
// Los puertos 7001 y 8001 se asignan como porteros
void sendInitCommand(MinimalSocket::udp::Udp<true> &udp_socket, const MinimalSocket::Address &server_udp, MinimalSocket::Port this_socket_port, std::string team_name)
{
    std::string init_msg;

    if (this_socket_port == 7001 || this_socket_port == 8001) {
        init_msg = "(init " + team_name + " (version 19) (goalie))";
    } else {
        init_msg = "(init " + team_name + " (version 19))";
    }

    std::cout << "Sending init message: " << init_msg << std::endl;
    udp_socket.sendTo(init_msg, server_udp);
    std::cout << "Init message sent" << std::endl;
}

// Envía el comando para posicionar al jugador en su ubicación inicial
void sendMoveCommand(PlayerInfo &player, MinimalSocket::udp::Udp<true> &udp_socket, const MinimalSocket::Address &server_udp)
{
    std::string move_cmd =
        "(move " + std::to_string(player.initialPosition.x) +
        " "      + std::to_string(player.initialPosition.y) + ")";

    std::cout << "Sending move command: " << move_cmd << std::endl;
    udp_socket.sendTo(move_cmd, server_udp);
    std::cout << "Move command sent" << std::endl;
}

// ---------- FUNCIONES DE DECISIÓN ----------

// Decide la acción a realizar basándose en la información visual del jugador
std::string decideAction(const PlayerInfo &player)
{
    std::string action_cmd;

        // Si no ve el balón, girar para buscarlo
        if (!player.see.ball.visible) {
            action_cmd = "(turn 30)";
        } else {
            double ball_dist = player.see.ball.dist;
            double ball_dir  = player.see.ball.dir;

            // Balón lejos: orientarse y correr hacia él
            if (ball_dist > 1.0) {
                if (std::abs(ball_dir) > 10.0) {
                    // Ajustar orientación hacia el balón (limitado a ±60°)
                    double turnAngle = ball_dir;
                    if (turnAngle > 60.0) turnAngle = 60.0;
                    if (turnAngle < -60.0) turnAngle = -60.0;

                    action_cmd = "(turn " + std::to_string(turnAngle) + ")";
                } else {
                    // Ya orientado, correr hacia el balón
                    action_cmd = "(dash 90)";
                }
            } else {
                // Balón cerca: intentar chutar
                if (player.see.opp_goal.visible) {
                    double kickDir = player.see.opp_goal.dir;
                    action_cmd = "(kick 90 " + std::to_string(kickDir) + ")";
                } else {
                    // Si no ve la portería rival, girar para buscarla
                    action_cmd = "(turn 30)";
                }
            }
        }
    return action_cmd;
}

int main(int argc, char *argv[])
{
    // Validar argumentos de línea de comandos
    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " <team-name> <this-port>" << std::endl;
        return 1;
    }

    std::string team_name = argv[1];
    MinimalSocket::Port this_socket_port = std::stoi(argv[2]);

    std::cout << "Creating a UDP socket on local port " << this_socket_port << std::endl;

    // Crear socket UDP para comunicación con el servidor
    MinimalSocket::udp::Udp<true> udp_socket(
        this_socket_port,
        MinimalSocket::AddressFamily::IP_V4
    );

    std::cout << "Socket created" << std::endl;

    bool success = udp_socket.open();
    if (!success) {
        std::cout << "Error opening socket" << std::endl;
        return 1;
    }

    // Dirección del servidor rcssserver (puerto estándar 6000)
    MinimalSocket::Address server_address{"127.0.0.1", 6000};

    sendInitCommand(udp_socket, server_address, this_socket_port, team_name);

    std::size_t message_max_size = 1000;
    std::cout << "Waiting for an init message from server..." << std::endl;

    // Recibir respuesta del servidor con la asignación de lado y dorsal
    auto received_message = udp_socket.receive(message_max_size);

    if (!received_message) {
        std::cerr << "Error receiving message from server" << std::endl;
        return 1;
    }

    std::string received_message_content = received_message->received_message;
    std::cout << "Received message: " << received_message_content << std::endl;

    // Usar el puerto específico del servidor para las comunicaciones posteriores
    MinimalSocket::Address other_sender_udp = received_message->sender;
    MinimalSocket::Address server_udp{"127.0.0.1", other_sender_udp.getPort()};

    // Parsear el mensaje de inicialización y configurar el jugador
    PlayerInfo player;
    parseInitMsg(received_message_content, player);
    std::cout << player << std::endl;

    sendMoveCommand(player, udp_socket, server_udp);

    // Bucle principal: recibir mensajes del servidor y actuar
    while(true) {
        auto msg = receiveMsgFromServer(udp_socket, message_max_size);

        bool shouldAct = false;

        if (msg.rfind("(see", 0) == 0) {
            parseSeeMsg(msg, player);
            // std::cout << "[DEBUG] " << player.see << std::endl;
            shouldAct = true;  // Actuar después de recibir información visual
        } else if (msg.rfind("(sense_body", 0) == 0) {
            parseSenseMsg(msg, player);
        } 

        if (shouldAct) {
            std::string action_cmd = decideAction(player);
            if (!action_cmd.empty()) {
                std::cout << "Sending command: " << action_cmd << std::endl;
                udp_socket.sendTo(action_cmd + '\0', server_udp);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return 0;
}
