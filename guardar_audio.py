import serial
import serial.tools.list_ports
import wave
import time
import sys

def find_esp_port():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if "USB" in port.description or "UART" in port.description or "CH340" in port.description or "CP210" in port.description or "JTAG" in port.description:
            return port.device
    if len(ports) > 0:
        return ports[0].device
    return None

def main():
    print("Buscando puerto serie...")
    port_name = find_esp_port()
    
    if not port_name:
        port_name = input("No se encontró automáticamente el puerto del ESP32. Por favor, ingrésalo (ej: COM3): ")
    else:
        print(f"Puerto detectado: {port_name}")
        respuesta = input("¿Es correcto? (S/n): ")
        if respuesta.lower() == 'n':
            port_name = input("Ingresa el puerto correcto (ej: COM3): ")

    baud_rate = 115200

    try:
        ser = serial.Serial(port_name, baud_rate, timeout=1)
        print(f"Conectado a {port_name} a {baud_rate} baudios.")
    except Exception as e:
        print(f"Error al abrir el puerto: {e}")
        print("Asegúrate de que el puerto esté libre y de que el monitor serie de ESP-IDF esté CERRADO.")
        sys.exit(1)

    print("Sincronizando con el ESP32 (esperando la siguiente grabación)...")
    
    # Limpiar buffer de entrada para no leer basura vieja
    ser.reset_input_buffer()
    
    in_audio_block = False
    hex_data = ""

    try:
        while True:
            line = ser.readline()
            if not line:
                continue
                
            try:
                text = line.decode('utf-8', errors='ignore').strip()
            except:
                continue

            if "3..." in text or "2..." in text or "1..." in text or "Preparando" in text:
                print(f"ESP32: {text.split('ESP32: ')[-1] if 'ESP32: ' in text else text}")
                continue

            if ">>> GRABANDO" in text:
                print("\n==============================================")
                print(">>> EL ESP32 ESTÁ GRABANDO AHORA MISMO <<<")
                print(">>> HABLA DURANTE LOS PRÓXIMOS 5 SEGUNDOS <<<")
                print("==============================================\n")
                continue

            if "---BEGIN_AUDIO---" in text:
                print("\n¡Comenzando a recibir datos de audio! Espere por favor...")
                in_audio_block = True
                hex_data = ""
                continue
                
            if "---END_AUDIO---" in text:
                if in_audio_block:
                    print("\n¡Datos recibidos completamente!")
                    break
                else:
                    # Se conectó a la mitad de una transmisión, ignorar este END
                    continue
                
            if in_audio_block:
                hex_data += text
                # Simple indicador de progreso
                if len(hex_data) % 32000 == 0:
                    print(".", end="", flush=True)
            else:
                # Imprimir los logs normales del ESP32 antes de grabar
                if text:
                    print(f"ESP32: {text}")
                    
    except KeyboardInterrupt:
        print("\nCancelado por el usuario.")
        ser.close()
        sys.exit(0)
        
    ser.close()

    print("\nProcesando y guardando archivo de audio...")
    
    # Convertir hexadecimal a bytes
    try:
        # Limpiamos el hex_data de cualquier espacio o salto de línea
        hex_data_clean = "".join(c for c in hex_data if c in "0123456789abcdefABCDEF")
        
        # Asegurarnos de que tenga longitud par
        if len(hex_data_clean) % 2 != 0:
            hex_data_clean = hex_data_clean[:-1]
            
        audio_bytes = bytes.fromhex(hex_data_clean)
        
        # El C printf("%04X") imprime en Big-Endian, pero los WAV necesitan Little-Endian.
        # Intercambiamos los bytes (pares con impares)
        if len(audio_bytes) % 2 != 0:
            audio_bytes = audio_bytes[:-1]
            
        swapped_bytes = bytearray(len(audio_bytes))
        swapped_bytes[0::2] = audio_bytes[1::2]
        swapped_bytes[1::2] = audio_bytes[0::2]
        audio_bytes = bytes(swapped_bytes)
        
        # Guardar archivo WAV
        with wave.open("grabacion.wav", "wb") as wav_file:
            wav_file.setnchannels(1) # Mono
            wav_file.setsampwidth(2) # 16 bits (2 bytes)
            wav_file.setframerate(16000) # 16000 Hz
            wav_file.writeframes(audio_bytes)
            
        print(f"¡Éxito! Audio guardado en 'grabacion.wav'")
        print(f"Tamaño del archivo: {len(audio_bytes) / 1024:.2f} KB")
        print("Ahora puedes abrir y escuchar 'grabacion.wav'")
        
    except Exception as e:
        print(f"Error procesando los datos: {e}")

if __name__ == "__main__":
    main()
