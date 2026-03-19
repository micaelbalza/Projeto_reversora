# 🚀 GUIA RÁPIDO - Sistema de Calibração ESP32

## Antes de Começar

**Requisitos:**
- ESP32 com USB conectado ao computador
- ESP-IDF instalado e configurado
- Navegador web no celular
- Python 3.8+ (para desenvolvimento)

---

## 1️⃣ Primeira Execução - Compilar e Flashear

### Opção A: Script Automatizado (Recomendado)
```bash
cd Desafio_05
bash setup_and_build.sh
# Selecione opção 7 para clean + build completo
# Depois selecione 5 para build + flash + monitor
```

### Opção B: Comandos Manuais
```bash
cd Desafio_05

# Limpar build anterior
idf.py fullclean

# Configurar (se necessário)
idf.py set-target esp32
idf.py menuconfig

# Compilar
idf.py build

# Flashear
idf.py flash

# Monitorar (Ctrl+] para sair)
idf.py monitor
```

---

## 2️⃣ Conectar com o Celular

### Primeira Inicialização (AP Mode)

1. **Abra WiFi do celular**
   - Procure por rede chamada **`ESP32-CALIB`**
   - Senha: **`12345678`**

2. **Abra navegador**
   - Acesse: **`http://192.168.4.1`**
   - Interface de configuração aparecerá

3. **Configure sua rede WiFi**
   - Digite SSID de sua rede local
   - Digite senha
   - Clique em **"Conectar"**

4. **Aguarde conexão**
   - ESP32 tenta conectar
   - Se bem-sucedido, novo IP aparecerá
   - Conecte-se à sua rede WiFi local

5. **Acesse novo IP**
   - Clique em **"Iniciar Calibração"**

---

## 3️⃣ Processo de Calibração

A interface guiará em 4 passos:

### Passo 1: Neutro
- Posicione reversora em **NEUTRO**
- Clique em **"Confirmar"**
- Aguarde processamento

### Passo 2: Avante
- Posicione em **AVANTE**
- Clique em **"Confirmar"**
- Aguarde

### Passo 3: Ré
- Posicione em **RÉ**
- Clique em **"Confirmar"**
- Aguarde

### Passo 4: Finalizar
- Sistema executa ajustes finais
- Clique em **"Confirmar"**
- Sucesso! ✨

---

## 🔄 Próximas Inicializações

A partir da segunda vez:
1. ESP32 carrega credenciais salvas
2. Conecta automaticamente à sua rede WiFi
3. Acesse novo IP direto (sem AP mode)
4. Interface de calibração aparece

**Para resetar credenciais:**
- Clique em **"Resetar Credenciais"** na interface
- Volta para AP mode
- Reconfigure WiFi novamente

---

## 🧪 Testar Sem Celular (Desenvolvimento)

### Teste de API com Script
```bash
cd Desafio_05

# Teste status WiFi
bash test_api.sh 192.168.4.1

# Selecione opção 10 para teste completo automatizado
```

### Teste Manual com curl
```bash
# Verificar status WiFi
curl http://192.168.4.1/api/wifi/status

# Configurar WiFi
curl -X POST http://192.168.4.1/api/wifi/config \
  -H "Content-Type: application/json" \
  -d '{"ssid":"MinhaRede","password":"Senha123"}'

# Iniciar calibração
curl -X POST http://192.168.4.1/api/calibration/start

# Ver status
curl http://192.168.4.1/api/calibration/status
```

---

## 📊 Monitorar Logs do ESP32

```bash
idf.py monitor

# Saída esperada:
# I (234) MAIN: === Sistema de Calibração ESP32 ===
# I (256) WIFI_MANAGER: Gerenciador WiFi inicializado
# I (278) MAIN: Sistema pronto!
# I (300) WEB_SERVER: Servidor HTTP iniciado
```

**Tags de log para filtrar:**
- `WIFI_MANAGER` - Eventos Wi-Fi
- `CALIBRATION` - Estados calibração
- `WEB_SERVER` - Requisições HTTP
- `MAIN` - Sistema geral

---

## 🔧 Personalizar Hardware

Suas funções de calibração:
1. Abra `main/calibration.c`
2. Procure por `calibrate_neutral()`, etc
3. Substitua simulações pelas suas funções reais
4. Ou veja `INTEGRATION_EXAMPLE.h` para exemplos

---

## 🐛 Troubleshooting

### Interface não carrega
- [ ] Verificar IP: `curl http://192.168.4.1/api/wifi/status`
- [ ] Limpar cache navegador: Ctrl+Shift+Del
- [ ] Verificar logs UART: `idf.py monitor`

### WiFi não conecta
- [ ] Verificar SSID/senha corretos
- [ ] Resetar credenciais via interface
- [ ] Verificar logs para detalhes

### Calibração não avança
- [ ] Verificar estado: `curl http://192.168.4.1/api/calibration/status`
- [ ] Reiniciar via botão "Reiniciar"
- [ ] Verificar logs: `idf.py monitor`

### Precisa resetar tudo
```bash
cd Desafio_05

# Limpar tudo
idf.py erase_flash

# Recompile e flash
idf.py build flash monitor
```

---

## 📱 Interface Web - Funcionalidades

- **Responsiva**: Funciona em qualquer tamanho de tela
- **Leve**: ~30KB de HTML/CSS/JS
- **Offline First**: Funciona mesmo com conexão instável
- **Feedback Visual**: Barra de progresso e indicadores
- **Telas Adaptáveis**: Muda conforme estado do sistema

---

## 🎯 Arquitetura do Código

```
Componentes principais:
├── wifi_manager.c/h      → Controla Wi-Fi
├── calibration.c/h       → Máquina de estados calibração
├── web_server.c/h        → Servidor HTTP e rotas
├── main.c                → Orquestra sistema
└── index.html            → Interface web

Cada módulo é independente e testável!
```

---

## 📞 Para Mais Informações

Veja arquivos de documentação:
- **README_SISTEMA.md** - Documentação técnica completa
- **INTEGRATION_EXAMPLE.h** - Exemplos de integração hardware
- **Logs UART** - Saída do `idf.py monitor`

---

## ✅ Checklist de Verificação

- [ ] ESP32 flasheado com sucesso
- [ ] Logs aparecem no monitor
- [ ] WiFi AP aparece no celular
- [ ] Interface web carrega em 192.168.4.1
- [ ] WiFi local configurado com sucesso
- [ ] Calibração completa sem erros
- [ ] Funções de hardware integradas

---

**Projeto pronto para uso! 🎉**

Data: 2026-02-09  
ESP-IDF: 4.4+  
Linguagem: C puro (sem Arduino/C++)
