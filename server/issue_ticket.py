import uuid
import qrcode
import os
from database import init_db, insert_ticket

OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "tickets")

def issue_ticket():
    init_db()
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    ticket_id = str(uuid.uuid4())
    insert_ticket(ticket_id)

    # QR 이미지 생성
    qr = qrcode.make(ticket_id)
    path = os.path.join(OUTPUT_DIR, f"{ticket_id}.png")
    qr.save(path)

    print(f"티켓 발급 완료")
    print(f"ID  : {ticket_id}")
    print(f"QR  : {path}")
    return ticket_id

if __name__ == "__main__":
    issue_ticket()
