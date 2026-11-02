#!/usr/bin/env bash
# Cai va dieu khien may chu MySQL trong thu muc du an (khong can sudo, khong can brew).
#   bash scripts/mysql.sh start    cai neu chua co, roi bat may chu
#   bash scripts/mysql.sh stop     tat may chu
#   bash scripts/mysql.sh status   xem may chu co dang chay khong
#   bash scripts/mysql.sh shell    mo dong lenh SQL
set -euo pipefail

MYSQL_VERSION=8.4.10
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HOME_DIR="$ROOT/deps/mysql"
DATA_DIR="$ROOT/data/mysql"
CONFIG="$ROOT/data/db.conf"
SOCKET="$DATA_DIR/mysql.sock"
PORT=3306
DATABASE=face_attendance

install_server() {
    [ -x "$HOME_DIR/bin/mysqld" ] && return
    local archive="$ROOT/deps/mysql.tar.gz"
    curl -fL --retry 3 -o "$archive" \
        "https://cdn.mysql.com/Downloads/MySQL-8.4/mysql-$MYSQL_VERSION-macos15-arm64.tar.gz"
    mkdir -p "$HOME_DIR"
    tar -xzf "$archive" -C "$HOME_DIR" --strip-components 1
    rm "$archive"
}

is_running() {
    "$HOME_DIR/bin/mysqladmin" --no-defaults --socket="$SOCKET" --connect-timeout=2 \
        ping >/dev/null 2>&1
}

start_server() {
    "$HOME_DIR/bin/mysqld" --no-defaults --daemonize \
        --basedir="$HOME_DIR" --datadir="$DATA_DIR" --socket="$SOCKET" \
        --pid-file="$DATA_DIR/mysqld.pid" --log-error="$DATA_DIR/error.log" \
        --bind-address=127.0.0.1 --port="$PORT" --mysqlx=OFF \
        --character-set-server=utf8mb4 --collation-server=utf8mb4_0900_ai_ci
    for _ in $(seq 1 30); do
        is_running && return
        sleep 1
    done
    echo "May chu khong khoi dong duoc, xem $DATA_DIR/error.log" >&2
    exit 1
}

# Tao co so du lieu va tai khoan rieng cho chuong trinh, mat khau ngau nhien
first_time_setup() {
    local password
    password="$(openssl rand -hex 16)"
    "$HOME_DIR/bin/mysql" --no-defaults --socket="$SOCKET" -uroot <<SQL
CREATE DATABASE $DATABASE;
CREATE DATABASE ${DATABASE}_test;
CREATE USER 'face_app'@'127.0.0.1' IDENTIFIED BY '$password';
GRANT ALL ON $DATABASE.* TO 'face_app'@'127.0.0.1';
GRANT ALL ON ${DATABASE}_test.* TO 'face_app'@'127.0.0.1';
ALTER USER 'root'@'localhost' IDENTIFIED BY '$password';
SQL
    (
        umask 077
        cat >"$CONFIG" <<CONF
host=127.0.0.1
port=$PORT
user=face_app
password=$password
database=$DATABASE
CONF
    )
}

password_from_config() {
    sed -n 's/^password=//p' "$CONFIG"
}

case "${1:-}" in
    start)
        install_server
        mkdir -p "$ROOT/data"
        if is_running; then
            echo "MySQL dang chay"
            exit 0
        fi
        if [ ! -d "$DATA_DIR" ]; then
            "$HOME_DIR/bin/mysqld" --no-defaults --initialize-insecure \
                --basedir="$HOME_DIR" --datadir="$DATA_DIR"
            start_server
            first_time_setup
        else
            start_server
        fi
        echo "MySQL da bat tai 127.0.0.1:$PORT"
        ;;
    stop)
        MYSQL_PWD="$(password_from_config)" "$HOME_DIR/bin/mysqladmin" --no-defaults \
            --socket="$SOCKET" -uroot shutdown
        echo "MySQL da tat"
        ;;
    status)
        if is_running; then echo "MySQL dang chay"; else echo "MySQL dang tat"; fi
        ;;
    shell)
        MYSQL_PWD="$(password_from_config)" exec "$HOME_DIR/bin/mysql" --no-defaults \
            --socket="$SOCKET" -uroot --default-character-set=utf8mb4 "$DATABASE"
        ;;
    *)
        echo "Cach dung: bash scripts/mysql.sh start|stop|status|shell" >&2
        exit 1
        ;;
esac
