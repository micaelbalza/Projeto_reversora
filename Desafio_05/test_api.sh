#!/bin/bash
# Script de teste de API
# Uso: bash test_api.sh <ip_esp32>
# Exemplo: bash test_api.sh 192.168.4.1

IP="${1:-192.168.4.1}"
PORT="${2:-80}"
BASE_URL="http://$IP:$PORT"

echo "=========================================="
echo "Teste de API - Sistema de Calibração"
echo "=========================================="
echo "Alvo: $BASE_URL"
echo ""

# Cores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

test_endpoint() {
    local method=$1
    local endpoint=$2
    local data=$3
    local description=$4
    
    echo -e "${YELLOW}[TEST]${NC} $description"
    
    if [ -z "$data" ]; then
        response=$(curl -s -X $method "$BASE_URL$endpoint" -H "Content-Type: application/json")
    else
        response=$(curl -s -X $method "$BASE_URL$endpoint" \
            -H "Content-Type: application/json" \
            -d "$data")
    fi
    
    echo "Status: HTTP (esperando JSON)"
    echo "Resposta:"
    echo "$response" | python3 -m json.tool 2>/dev/null || echo "$response"
    echo ""
}

# Menu de testes
echo "Selecione testes a executar:"
echo "1) Status WiFi"
echo "2) Configurar WiFi"
echo "3) Status Calibração"
echo "4) Iniciar Calibração"
echo "5) Confirmar Neutro"
echo "6) Confirmar Avante"
echo "7) Confirmar Ré"
echo "8) Finalizar Calibração"
echo "9) Resetar WiFi"
echo "10) Teste completo"
echo "11) Sair"
echo ""

while true; do
    read -p "Opção (1-11): " choice
    
    case $choice in
        1)
            test_endpoint "GET" "/api/wifi/status" "" "GET WiFi Status"
            ;;
        2)
            read -p "SSID: " ssid
            read -p "Senha: " password
            data="{\"ssid\":\"$ssid\",\"password\":\"$password\"}"
            test_endpoint "POST" "/api/wifi/config" "$data" "POST WiFi Config"
            ;;
        3)
            test_endpoint "GET" "/api/calibration/status" "" "GET Calibration Status"
            ;;
        4)
            test_endpoint "POST" "/api/calibration/start" "" "POST Start Calibration"
            ;;
        5)
            test_endpoint "POST" "/api/calibration/neutral" "" "POST Confirm Neutral"
            sleep 2
            test_endpoint "GET" "/api/calibration/status" "" "GET Status após Neutral"
            ;;
        6)
            test_endpoint "POST" "/api/calibration/forward" "" "POST Confirm Forward"
            sleep 2
            test_endpoint "GET" "/api/calibration/status" "" "GET Status após Forward"
            ;;
        7)
            test_endpoint "POST" "/api/calibration/reverse" "" "POST Confirm Reverse"
            sleep 2
            test_endpoint "GET" "/api/calibration/status" "" "GET Status após Reverse"
            ;;
        8)
            test_endpoint "POST" "/api/calibration/finish" "" "POST Finish Calibration"
            ;;
        9)
            test_endpoint "POST" "/api/wifi/reset" "" "POST Reset WiFi"
            ;;
        10)
            echo -e "${GREEN}Iniciando teste completo...${NC}"
            echo ""
            
            test_endpoint "GET" "/api/wifi/status" "" "1. Verificar status WiFi"
            sleep 1
            
            test_endpoint "POST" "/api/calibration/start" "" "2. Iniciar calibração"
            sleep 1
            
            test_endpoint "POST" "/api/calibration/neutral" "" "3. Confirmar Neutro"
            sleep 2
            
            test_endpoint "POST" "/api/calibration/forward" "" "4. Confirmar Avante"
            sleep 2
            
            test_endpoint "POST" "/api/calibration/reverse" "" "5. Confirmar Ré"
            sleep 2
            
            test_endpoint "POST" "/api/calibration/finish" "" "6. Finalizar"
            sleep 1
            
            test_endpoint "GET" "/api/calibration/status" "" "7. Status final"
            
            echo -e "${GREEN}Teste completo finalizado!${NC}"
            ;;
        11)
            echo "Saindo..."
            exit 0
            ;;
        *)
            echo -e "${RED}Opção inválida!${NC}"
            ;;
    esac
    
    echo ""
done
