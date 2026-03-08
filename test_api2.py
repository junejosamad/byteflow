import requests
import sys

try:
    res = requests.post('http://127.0.0.1:8000/api/v1/flow/run_all', files={'netlist': open('benchmarks/soc_sram.v', 'rb')})
    print('TEST RESULT HTTP:', res.status_code)
except Exception as e:
    print('Request failed:', e)
