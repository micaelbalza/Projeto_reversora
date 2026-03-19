# Sistema de Calibração com ESP32 e ESP-IDF

## 📋 Visão Geral

Sistema embarcado modular para ESP32 que implementa:
- **Gerenciamento de Wi-Fi** com alternância automática entre modos AP e STA
- **Interface Web Responsiva** servida diretamente pelo ESP32
- **Configuração de Rede Segura** com persistência em NVS (Non-Volatile Storage)
- **Processo de Calibração Guiado** com máquina de estados
- **API REST** para comunicação HTTP entre celular e ESP32

## 🏗️ Arquitetura Modular

O projeto é organizado em módulos independentes e reutilizáveis:

### 📦 Módulo `wifi_manager` (wifi_manager.c/h)
**Responsabilidades:**
- Inicialização do Wi-Fi (STA e AP)
- Alternância automática entre modos de operação
- Persistência de credenciais em NVS
- Tratamento de falhas de conexão com retry
- Callbacks para notificação de mudanças de estado

**Funções Principais:**
```c
esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_connect_sta(const char *ssid, const char *password);
esp_err_t wifi_manager_start_ap(void);
esp_err_t wifi_manager_reset_credentials(void);
wifi_state_t wifi_manager_get_state(void);
const char *wifi_manager_get_ip(void);
```

**Estados:**
- `WIFI_DISCONNECTED` - Desconectado
- `WIFI_CONNECTING` - Conectando
- `WIFI_AP_MODE` - Modo ponto de acesso (192.168.4.1)
- `WIFI_STA_CONNECTED` - Conectado à rede local
- `WIFI_ERROR` - Erro

### 🔧 Módulo `calibration` (calibration.c/h)
**Responsabilidades:**
- Máquina de estados para processo de calibração
- Validação de transições de estado
- Orquestração das funções de calibração
- Cálculo de progresso
- Callbacks para notificação de mudanças

**Funções Principais:**
```c
esp_err_t calibration_init(void);
esp_err_t calibration_start(void);
esp_err_t calibration_confirm_neutral(void);
esp_err_t calibration_confirm_forward(void);
esp_err_t calibration_confirm_reverse(void);
esp_err_t calibration_finish(void);
esp_err_t calibration_reset(void);
calibration_state_t calibration_get_state(void);
int calibration_get_progress(void);
```

**Estados da Calibração:**
```
IDLE
  ↓
WAIT_NEUTRAL → NEUTRAL_DONE
  ↓
WAIT_FORWARD → FORWARD_DONE
  ↓
WAIT_REVERSE → REVERSE_DONE
  ↓
FINALIZING → COMPLETED
```

### 🌐 Módulo `web_server` (web_server.c/h)
**Responsabilidades:**
- Servidor HTTP esp_http_server
- Rotas REST para Wi-Fi e calibração
- Servir interface web HTML
- Processamento de requisições JSON
- CORS e segurança

**Endpoints da API:**

#### Wi-Fi
- `GET /` - Serve página HTML principal
- `GET /api/wifi/status` - Status da conexão Wi-Fi
- `POST /api/wifi/config` - Configura credenciais
- `POST /api/wifi/reset` - Reseta credenciais

#### Calibração
- `GET /api/calibration/status` - Status e progresso
- `POST /api/calibration/start` - Inicia calibração
- `POST /api/calibration/neutral` - Confirma posição neutra
- `POST /api/calibration/forward` - Confirma posição avante
- `POST /api/calibration/reverse` - Confirma posição ré
- `POST /api/calibration/finish` - Finaliza calibração

**Formato de Requisição (JSON):**
```json
{
  "ssid": "MinhaRede",
  "password": "Senha123"
}
```

**Formato de Resposta:**
```json
{
  "status": "ok",
  "message": "Descrição da resposta"
}
```

### 🎯 Módulo `main` (main.c)
**Responsabilidades:**
- Inicialização do sistema
- Orquestração de módulos
- Task de monitoramento
- Logging de status do sistema

## 🚀 Fluxo de Funcionamento

### Primeira Inicialização
```
1. ESP32 inicia
2. NVS é inicializado
3. Módulos de calibração e Wi-Fi inicializam
4. Como não há credenciais salvas → Inicia modo AP
5. ESP32 fica em 192.168.4.1 com SSID "ESP32-CALIB"
6. Servidor HTTP inicia e aguarda requisições
7. Usuário acessa http://192.168.4.1 via celular
8. Interface web permite configurar Wi-Fi local
```

### Conexão e Calibração
```
1. Usuário insere SSID e senha da rede local
2. Credenciais são salvas em NVS
3. ESP32 tenta conectar como STA
4. Após conectar, interface web exibe novo IP
5. Usuário clica "Iniciar Calibração"
6. Interface guia passo-a-passo: Neutro → Avante → Ré → Finalizar
7. Cada confirmação executa função correspondente
8. Ao concluir, interface exibe sucesso
```

### Inicializações Subsequentes
```
1. ESP32 inicia
2. Credenciais são lidas da NVS
3. Tenta conectar como STA com credenciais salvas
4. Se conectar com sucesso → Modo STA e IP dinâmico
5. Se falhar 5 vezes → Volta para modo AP
```

## 🛠️ Compilação e Execução

### Pré-requisitos
- ESP-IDF 4.4 ou superior
- Python 3.8+
- ESP32 com conexão USB/JTAG

### Configuração Inicial
```bash
# Configurar projeto
idf.py set-target esp32
idf.py menuconfig

# Recomendar:
# - Configurar UART baud rate
# - Ajustar tamanho da partição se necessário
```

### Compilação
```bash
idf.py build
```

### Flash
```bash
idf.py flash
```

### Monitorar
```bash
idf.py monitor
```

### Compilar, Flash e Monitorar
```bash
idf.py build flash monitor
```

## 📱 Interface Web

### Design Responsivo
- Otimizado para telas pequenas (celulares)
- Gradientes e animações suaves
- Feedback visual de progresso
- Estados visuais para cada fase

### Funcionalidades

#### Tela de Configuração Wi-Fi
- Input para SSID
- Input para Senha (mascarado)
- Botão de Conexão
- Indicador de status em tempo real
- Botão para resetar credenciais

#### Tela de Calibração
- Barra de progresso visual
- Indicador de passos (1, 2, 3, ✓)
- Instruções contextuais
- Botão de confirmação
- Botão de reinicialização

#### Tela de Sucesso
- Mensagem de conclusão
- Opção de voltar ao início

## 🔐 Segurança e Robustez

### Validação de Entrada
- Validação de tamanho de SSID e senha
- Verificação de integridade de JSON
- Tratamento de requisições malformadas

### Tratamento de Erros
- Retry automático de conexão Wi-Fi (5 tentativas)
- Fallback para AP mode em caso de falha
- Validação de transições de estado
- Prevenção de execução fora de sequência

### Persistência
- Credenciais salvas em NVS
- Recuperação automática em reinicializações
- Limpeza segura de dados sensíveis

## 📊 Logging

Todos os módulos utilizam `ESP_LOG` com tags específicas:
- `WIFI_MANAGER` - Eventos de Wi-Fi
- `CALIBRATION` - Estados de calibração
- `WEB_SERVER` - Requisições HTTP
- `MAIN` - Inicialização e monitoramento

### Exemplo de Output
```
I (234) MAIN: === Sistema de Calibração ESP32 ===
I (245) MAIN: Inicializando módulos...
I (256) WIFI_MANAGER: Inicializando gerenciador de Wi-Fi
I (267) WIFI_MANAGER: Credenciais não encontradas. Iniciando modo AP
I (278) WIFI_MANAGER: Estado: AP_MODE com IP 192.168.4.1
I (289) WEB_SERVER: Iniciando servidor HTTP
I (300) WEB_SERVER: Rotas registradas com sucesso
```

## 🔄 Extensões Futuras

### Melhorias Planejadas
- [ ] Dashboard com estatísticas de calibração
- [ ] Histórico de calibrações em SPIFFS
- [ ] OTA (Over-The-Air) firmware updates
- [ ] Modo sleep para economia de energia
- [ ] Autenticação por token JWT
- [ ] Comunicação MQTT
- [ ] Armazenamento em SD card
- [ ] HTTPS com certificados autoassinados

### Pontos de Extensão
1. **Novos Sensores**: Adicionar novos handlers de hardware no módulo calibration
2. **Novos Endpoints**: Registrar novas rotas em web_server.c
3. **Novos Estados**: Estender máquina de estados em calibration.c
4. **Integração com NVS**: Armazenar mais dados calibrados

## 📚 Estrutura de Arquivos

```
main/
├── CMakeLists.txt              # Configuração de build
├── main.c                      # Ponto de entrada e orquestração
├── wifi_manager.c/.h           # Gerenciador de Wi-Fi
├── calibration.c/.h            # Lógica de calibração
├── web_server.c/.h             # Servidor HTTP e rotas
└── index.html                  # Interface web (embarcada)
```

## 🐛 Troubleshooting

### ESP32 não conecta ao Wi-Fi
- Verificar se as credenciais estão corretas
- Resetar credenciais: POST `/api/wifi/reset`
- Consultar logs de UART para detalhes

### Interface web não carrega
- Verificar IP do ESP32 (GET `/api/wifi/status`)
- Limpar cache do navegador (Ctrl+Shift+Del)
- Verificar se servidor HTTP iniciou (logs UART)

### Calibração não avança de estado
- Verificar logs para sequência de estados
- Resetar calibração via botão na interface
- Verificar se funções de calibração estão implementadas

### Bluetooth/Serial não funciona
- Verificar configuração de UART em menuconfig
- Usar baud rate 115200 padrão

## 📞 Contato e Suporte

Para dúvidas ou problemas:
1. Verificar logs de UART completos
2. Consultar documentação do ESP-IDF
3. Revisar código-fonte comentado

## 📄 Licença

Este projeto é fornecido como-é para fins educacionais.

---

**Criado para:** Embarcatech - IFRN  
**Data:** 2026-02-09  
**ESP-IDF Version:** 4.4+
