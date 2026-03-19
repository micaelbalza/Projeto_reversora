# INTEGRAÇÃO DO BROKER COM O HOME ASSISTANT

## Abordagem inicial

Inicialmente, a ideia é criar um container Docker com uma imagem do Home Assistant.

Ao realizar o acesso, configurar a integração com o broker MQTT e criar os cards para plotar os dados recebidos.

```bash
mkdir home-assistant
cd home-assistant

docker run -d \
  --name homeassistant \
  --restart unless-stopped \
  -e TZ=America/Sao_Paulo \
  -v $(pwd)/config:/config \
  --network=host \
  ghcr.io/home-assistant/home-assistant:stable
```

Para acessar: **localhost** ou o **IPV4** da máquina que hospeda o container + porta **8123**

O volume criado salva todas as configurações e dados na pasta **home-assistant**.

## Credenciais MQTT

```
host: mqtt.iot.natal.br
port: 1883
username: desafio05
password: desafio05.laica
```