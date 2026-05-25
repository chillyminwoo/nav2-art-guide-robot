import sqlite3
import os

DB_PATH = os.path.join(os.path.dirname(__file__), "tickets.db")

def get_conn():
    return sqlite3.connect(DB_PATH)

def init_db():
    with get_conn() as conn:
        conn.execute("""
            CREATE TABLE IF NOT EXISTS tickets (
                id         TEXT PRIMARY KEY,
                used       INTEGER DEFAULT 0,
                created_at TEXT DEFAULT (datetime('now', 'localtime')),
                used_at    TEXT
            )
        """)
        conn.commit()
    print(f"DB 초기화 완료: {DB_PATH}")

def insert_ticket(ticket_id: str):
    with get_conn() as conn:
        conn.execute("INSERT INTO tickets (id) VALUES (?)", (ticket_id,))
        conn.commit()

def verify_ticket(ticket_id: str) -> dict:
    with get_conn() as conn:
        row = conn.execute(
            "SELECT id, used FROM tickets WHERE id = ?", (ticket_id,)
        ).fetchone()

        if row is None:
            return {"valid": False, "reason": "존재하지 않는 티켓"}
        if row[1] == 1:
            return {"valid": False, "reason": "이미 사용된 티켓"}

        conn.execute(
            "UPDATE tickets SET used = 1, used_at = datetime('now', 'localtime') WHERE id = ?",
            (ticket_id,)
        )
        conn.commit()
        return {"valid": True, "reason": "인증 성공"}

if __name__ == "__main__":
    init_db()
