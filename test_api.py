import threading
import uvicorn
import time
import requests
import sys
from server import app

def run_server():
    uvicorn.run(app, host='127.0.0.1', port=8001, log_level='error')

server_thread = threading.Thread(target=run_server, daemon=True)
server_thread.start()

time.sleep(3)
try:
    res = requests.post('http://127.0.0.1:8001/api/v1/flow/run_all', files={'netlist': open('benchmarks/soc_sram.v', 'rb')})
    print('TEST RESULT HTTP:', res.status_code)
except Exception as e:
    print('Request failed:', e)
