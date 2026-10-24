#!/usr/bin/env python3
"""
MNEM Client de teste em Python.
Permite executar comandos isolados ou iniciar um console interativo (REPL).
Uso:
    python3 client.py [host] [port]
    python3 client.py SET usuario Allan EX 60
    python3 client.py GET usuario
"""

import sys
import socket
import json


def print_cli_help() -> None:
    print(
        "Uso:\n"
        "  Modo interativo:\n"
        "    python client.py [host] [port]\n"
        "  Modo comando único:\n"
        "    python client.py [host] [port] <COMANDO> [ARGUMENTOS...]\n"
        "    python client.py -p <porta> <COMANDO> [ARGUMENTOS...]\n"
        "  Exemplos:\n"
        "    python client.py PING\n"
        "    python client.py SET usuario Allan\n"
        "    python client.py SET sessao token123 EX 60\n"
        "    python client.py SET cache dados PX 5000 NX\n"
        "    python client.py GET usuario\n"
        "    python client.py EXPIRE usuario 120\n"
        "    python client.py INCR visitas 5\n"
        "    python client.py TTL sessao\n"
        "    python client.py HELP"
    )


def format_typed_value(data: str) -> str:
    # 0. Textos multilinha (ex: HELP, INFO) são exibidos sem aspas externas
    if "\n" in data:
        return data.rstrip("\r\n")

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


def read_line(sock: socket.socket) -> bytes:
    buf = bytearray()
    while True:
        b = sock.recv(1)
        if not b:
            break
        if b == b"\r":
            nxt = sock.recv(1)
            if not nxt or nxt == b"\n":
                break
            buf.extend(b)
            buf.extend(nxt)
        elif b == b"\n":
            break
        else:
            buf.extend(b)
    return bytes(buf)


def read_exact(sock: socket.socket, count: int) -> bytes:
    buf = bytearray()
    while len(buf) < count:
        chunk = sock.recv(count - len(buf))
        if not chunk:
            break
        buf.extend(chunk)
    return bytes(buf)


def read_response(sock: socket.socket) -> str:
    prefix = sock.recv(1)
    if not prefix:
        return "(conexão encerrada)"

    if prefix == b"+":  # Simple string
        line = read_line(sock).decode("utf-8", errors="replace")
        return line
    elif prefix == b"-":  # Error
        line = read_line(sock).decode("utf-8", errors="replace")
        return f"(error) {line}"
    elif prefix == b":":  # Integer
        line = read_line(sock).decode("utf-8", errors="replace")
        return f"(integer) {line}"
    elif prefix == b"$":  # Bulk string
        len_line = read_line(sock).decode("utf-8", errors="replace")
        length = int(len_line)
        if length == -1:
            return "(nil)"
        data = read_exact(sock, length)
        # Consome o \r\n final
        cr = sock.recv(1)
        if cr == b"\r":
            sock.recv(1)
        return format_typed_value(data.decode("utf-8", errors="replace"))
    elif prefix == b"*":  # Array
        count_line = read_line(sock).decode("utf-8", errors="replace")
        count = int(count_line)
        if count == -1:
            return "(nil)"
        results = []
        for i in range(1, count + 1):
            sub = read_response(sock)
            results.append(f"{i}) {sub}")
        return "\n".join(results)
    else:
        rest = read_line(sock).decode("utf-8", errors="replace")
        return prefix.decode("utf-8", errors="replace") + rest


def send_command(sock: socket.socket, cmd: str) -> str:
    if not cmd.endswith("\r\n"):
        cmd += "\r\n"
    sock.sendall(cmd.encode("utf-8"))
    return read_response(sock)


def build_resp_array(tokens: list[str]) -> str:
    parts = [f"*{len(tokens)}\r\n"]
    for tok in tokens:
        encoded = tok.encode("utf-8")
        parts.append(f"${len(encoded)}\r\n{tok}\r\n")
    return "".join(parts)


def main():
    host = "127.0.0.1"
    port = 6379
    args = sys.argv[1:]

    # Parse de porta / host se especificados
    cmd_tokens = []
    i = 0
    while i < len(args):
        arg = args[i]
        if not cmd_tokens and arg in ("--help", "-h"):
            if arg == "-h" and i + 1 < len(args) and not args[i + 1].startswith("-"):
                host = args[i + 1]
                i += 2
                continue
            print_cli_help()
            return
        elif not cmd_tokens and arg == "-p" and i + 1 < len(args):
            port = int(args[i + 1])
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
        array_cmd = build_resp_array(cmd_tokens)
        resp = send_command(sock, array_cmd)
        print(resp)
        sock.close()
        return

    print("=========================================================")
    print("  MNEM Python Client")
    print(f"  Conectado em {host}:{port}")
    print("  Digite comandos (ex: SET chave valor [EX seg], GET chave, HELP)")
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
