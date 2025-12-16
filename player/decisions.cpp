#include "decisions.h"
#include "positions.h"
#include <cmath>

// Verifica si el jugador está dentro de su zona asignada
bool estaEnZona(PlayerInfo& player){
    Zona z = definirZonaJugador(player);

    double margen = 1.5;  // Margen para evitar estar justo en el borde

    return (player.x_abs >= z.x_min + margen &&
            player.x_abs <= z.x_max - margen &&
            player.y_abs >= z.y_min + margen &&
            player.y_abs <= z.y_max - margen);
}

// Obtiene el centro de la zona del jugador
Point centroZona(PlayerInfo& player){
    Zona z = definirZonaJugador(player);
    return {
        (z.x_min + z.x_max) / 2.0,
        (z.y_min + z.y_max) / 2.0
    };
}

// Calcula el ángulo hacia un punto dado (en grados)
double anguloHacia(double x, double y, double xt, double yt){
    return atan2(yt - y, xt - x) * 180.0 / M_PI;
}

std::string playOnDecision(PlayerInfo &player)
{
    std::string action_cmd{""};

    // VOLVER A ZONA
    if (!estaEnZona(player))
    {
        Point centro = centroZona(player);

        // Angulo absoluto hacia el centro (Matemático CCW)
        double angAbs = anguloHacia(player.x_abs, player.y_abs,centro.x, centro.y);

        // Angulo relativo necesario (Matemático CCW)
        double angRel = normalizaAngulo(angAbs - player.dir_abs);

        // Invertimos el signo para el comando.
        double cmdAngle = -angRel; 

        // Si el ángulo es grande, GIRAR primero para no irse hacia atrás/lateral
        if (std::abs(angRel) > 45.0){
            action_cmd = "(turn " + std::to_string(cmdAngle) + ")";
        }
        else {
            // Dash hacia el objetivo
            action_cmd = "(dash 100 " + std::to_string(cmdAngle) + ")";
        }

        return action_cmd;
    }

    // COMPORTAMIENTO CON BALÓN
    
    if (!player.see.ball.visible)
    {
        action_cmd = "(turn 90)"; // Buscar balón
    }
    else
    {
        // Si el balón está lejos, ir hacia él
        if (player.see.ball.dist > 1){ 
            action_cmd = "(dash 100 " + std::to_string(player.see.ball.dir) + ")";
        }
        else 
        {
            // --- LÓGICA DE DISPARO (CORREGIDA) ---
            // Tenemos el balón controlado (dist <= 1.0)
            
            double kickAngle;
            
            // OPCIÓN A: Veo la portería -> Uso el dato visual (más preciso a corto plazo)
            if (player.see.oppGoal.visible)
            {
                kickAngle = player.see.oppGoal.dir;
            }
            // OPCIÓN B: No veo la portería -> Uso MATEMÁTICAS (Coordenadas absolutas)
            else
            {
                double x_porteria = 52.5; 
                double y_porteria = 0.0;
                if (player.side == Side::Right) {
                    x_porteria = -52.5; // Invertir X para el lado derecho
                }

                // B. CALCULO DEL TIRO
                // Ángulo global desde el robot hasta el centro de la portería
                double anguloGlobalMeta = anguloHacia(player.x_abs, player.y_abs, x_porteria, y_porteria);
                
                // Ángulo relativo (cuánto debo girar el pie respecto a mi cuerpo)
                kickAngle = anguloGlobalMeta - player.dir_abs;

                // C. NORMALIZACIÓN (EL PASO QUE FALTABA)
                // Esto asegura que si la resta da 350, se convierta en -10
                kickAngle = -normalizaAngulo(kickAngle);
            }

            // Ejecutamos el tiro con el ángulo decidido (sea visual o calculado)
            action_cmd = "(kick 100 " + std::to_string(kickAngle) + ")";
        }
    }
    return action_cmd;
}

std::string beforeKickOffDecision(PlayerInfo &player)
{
    return "(move " + std::to_string(player.initialPosition.x) + " " + std::to_string(player.initialPosition.y) + ")";
}

std::string decideAction(PlayerInfo &player, const GameState &gameState)
{
    if (gameState.playMode == PlayMode::PlayOn) {
        return playOnDecision(player);
    } if (gameState.playMode == PlayMode::BeforeKickOff ||
               gameState.playMode == PlayMode::Goal_Left ||
               gameState.playMode == PlayMode::Goal_Right) {
        return beforeKickOffDecision(player);
    } if (gameState.playMode == PlayMode::KickOff_Left ||
               gameState.playMode == PlayMode::KickOff_Right) {
        if (!player.see.ball.visible)
            return "(turn 90)";
        else
            return "(turn " + std::to_string(player.see.ball.dir) + ")";
    }
    
    return "";
}