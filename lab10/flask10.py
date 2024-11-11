#!/usr/bin/python3
from http import HTTPStatus

from flask import Flask
from flask import request
from flask import abort

app = Flask(__name__)

@app.route('/', methods=['POST'])
def calculation_POST():
  json_data = request.get_json()
  required_data = ['arg1', 'arg2', 'op']

  for rd in required_data:
    if rd not in json_data:
      abort(HTTPStatus.BAD_REQUEST)

  arg1 = int(json_data.get('arg1'))
  arg2 = int(json_data.get('arg2'))
  op = json_data.get('op')

  res = 0
  if op == "+":
    res = arg1 + arg2
  elif op == "-":
    res = arg1 - arg2
  elif op == "*":
    res = arg1 * arg2
  else:
    abort(HTTPStatus.BAD_REQUEST)
  
  return str(res) + "\n", HTTPStatus.OK

@app.route('/<int:arg1>/<op>/<int:arg2>', methods=['GET'])
def calculation_GET(arg1, op, arg2):
  res = 0
  if op == "+":
    res = arg1 + arg2
  elif op == "-":
    res = arg1 - arg2
  elif op == "*":
    res = arg1 * arg2
  else:
    abort(HTTPStatus.BAD_REQUEST)

  return str(res) + "\n", HTTPStatus.OK

if __name__ == '__main__':
  app.run(host='0.0.0.0', port=10221)
