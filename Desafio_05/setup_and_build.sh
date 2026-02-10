#!/bin/bash
# Script de setup e build do projeto ESP32 Calibration
# Uso: bash setup_and_build.sh

set -e

echo "================================"
echo "ESP32 Calibration System - Setup"
echo "================================"
echo ""

# Verificar se estamos no diretório correto
if [ ! -f "CMakeLists.txt" ]; then
    echo "❌ Erro: CMakeLists.txt não encontrado!"
    echo "Execute este script no diretório raiz do projeto."
    exit 1
fi

echo "✓ Diretório verificado"

# Verificar ESP-IDF
if [ -z "$IDF_PATH" ]; then
    echo "⚠️  Aviso: IDF_PATH não definido"
    echo "Certifique-se de que ESP-IDF está instalado e sourced"
    exit 1
fi

echo "✓ ESP-IDF: $IDF_PATH"

# Menu de opções
echo ""
echo "Selecione uma ação:"
echo "1) Limpar build anterior"
echo "2) Configurar projeto (menuconfig)"
echo "3) Compilar projeto"
echo "4) Compilar + Flash"
echo "5) Compilar + Flash + Monitor"
echo "6) Apenas monitorar"
echo "7) Clean + Build completo"
echo "8) Sair"
echo ""

read -p "Opção (1-8): " choice

case $choice in
    1)
        echo "Limpando build..."
        rm -rf build/
        echo "✓ Build limpo"
        ;;
    2)
        echo "Abrindo menuconfig..."
        idf.py menuconfig
        ;;
    3)
        echo "Compilando projeto..."
        idf.py build
        echo "✓ Compilação concluída"
        ;;
    4)
        echo "Compilando e flasheando..."
        idf.py build flash
        echo "✓ Flash concluído"
        ;;
    5)
        echo "Compilando, flasheando e monitorando..."
        idf.py build flash monitor
        ;;
    6)
        echo "Iniciando monitor (Ctrl+] para sair)..."
        idf.py monitor
        ;;
    7)
        echo "Limpeza completa e build..."
        rm -rf build/
        idf.py clean
        idf.py build
        echo "✓ Build completo concluído"
        ;;
    8)
        echo "Saindo..."
        exit 0
        ;;
    *)
        echo "Opção inválida!"
        exit 1
        ;;
esac

echo ""
echo "================================"
echo "Próximas etapas:"
echo "1. Acesse http://192.168.4.1 no seu celular"
echo "2. Configure sua rede WiFi"
echo "3. Acesse a interface de calibração"
echo "================================"
