#!/usr/bin/env python3
"""
MNEM Client de teste em Python.
Permite executar comandos isolados ou iniciar um console interativo (REPL).
Uso:
    python3 client.py [host] [port]
    python3 client.py SET usuario Allan
    python3 client.py GET usuario
"""

import sys
import socket
import json

def format_typed_value(data: str) -> str:
    # 1. Checa se é número (inteiro ou ponto flutuante)
    try:
        int(data)
        return data
    except ValueError:
        pass

    try:
        float(data)
        return data
    except ValueError:
        pass

    # 2. Checa se é JSON
    stripped = data.strip()
    if (stripped.startswith("{") and stripped.endswith("}")) or (stripped.startswith("[") and stripped.endswith("]")):
        try:
            json.loads(stripped)
            return data
        except Exception:
            pass

    # 3. Checa se é booleano ou null
    if data.lower() in ("true", "false", "null"):
        return data

    # 4. String convencional (com aspas)
    return f'"{data}"'

def send_command(sock: socket.socket, cmd: str) -> str:
    if not cmd.endswith("\r\n"):
        cmd += "\r\n"
    sock.sendall(cmd.encode("utf-8"))
    
    # Leitura da resposta
    reader = sock.makefile("rb")
    prefix = reader.read(1)
    if not prefix:
        return "(conexão encerrada)"
    
    if prefix == b"+":  # Simple string
        line = reader.readline().decode("utf-8").rstrip("\r\n")
        return line
    elif prefix == b"-":  # Error
        line = reader.readline().decode("utf-8").rstrip("\r\n")
        return f"(error) {line}"
    elif prefix == b":":  # Integer
        line = reader.readline().decode("utf-8").rstrip("\r\n")
        return f"(integer) {line}"
    elif prefix == b"$":  # Bulk string
        len_line = reader.readline().decode("utf-8").rstrip("\r\n")
        length = int(len_line)
        if length == -1:
            return "(nil)"
        data = reader.read(length)
        reader.readline()  # consome \r\n
        return format_typed_value(data.decode("utf-8"))
    elif prefix == b"*":  # Array
        count_line = reader.readline().decode("utf-8").rstrip("\r\n")
        count = int(count_line)
        if count == -1:
            return "(nil)"
        results = []
        for i in range(1, count + 1):
            sub = reader.readline().decode("utf-8").rstrip("\r\n")
            results.append(f"{i}) {sub}")
        return "\n".join(results)
    else:
        rest = reader.readline().decode("utf-8").rstrip("\r\n")
        return prefix.decode("utf-8") + rest

def main():
    host = "127.0.0.1"
    port = 6379
    args = sys.argv[1:]

    # Parse de porta / host se especificados
    cmd_tokens = []
    i = 0
    while i < len(args):
        arg = args[i]
        if arg == "-p" and i + 1 < len(args):
            port = int(args[i + 1])
            i += 2
            continue
        elif arg == "-h" and i + 1 < len(args):
            host = args[i + 1]
            i += 2
            continue
        elif not cmd_tokens and "." in arg:
            host = arg
        elif not cmd_tokens and arg.isdigit():
            port = int(arg)
        else:
            cmd_tokens.append(arg)
        i += 1

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((host, port))
    except Exception as e:
        print(f"[Client] Erro ao conectar com {host}:{port}: {e}")
        sys.exit(1)

    if cmd_tokens:
        cmd = " ".join(cmd_tokens)
        resp = send_command(sock, cmd)
        print(resp)
        sock.close()
        return

    print("=========================================================")
    print(f"  MNEM Python Client")
    print(f"  Conectado em {host}:{port}")
    print("  Digite 'quit' ou 'exit' para sair.")
    print("=========================================================")

    try:
        while True:
            try:
                line = input(f"{host}:{port}> ").strip()
            except (EOFError, KeyboardInterrupt):
                print()
                break

            if not line:
                continue

            if line.lower() in ("quit", "exit"):
                send_command(sock, "QUIT")
                print("Conexão encerrada.")
                break

            resp = send_command(sock, line)
            print(resp)
    finally:
        sock.close()

if __name__ == "__main__":
    main()
