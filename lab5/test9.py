import json
import sys

def main(argv):
  obj1 = {
    'name' : 'MJ Kim',
    'id' : 12345678,
    'work' : {
      'name' : 'Myongji University',
      'address' : '116 Myongji-ro'
    }
  }
  
  s = json.dumps(obj1)
  print(s)

if __name__ == '__main__':
  main(sys.argv)