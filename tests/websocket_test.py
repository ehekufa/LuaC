#!/usr/bin/env python3
"""Independent RFC 6455 peer for LuaC client/server and optional WSS tests."""
import argparse, base64, hashlib, os, socket, ssl, struct, subprocess, threading, time
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser();parser.add_argument('--secure',action='store_true');parser.add_argument('--client-only',action='store_true');args=parser.parse_args()
compiler=os.environ.get('LUAC','./luac');executable=os.environ.get('LUAC_EXECUTABLE')
root=ROOT/'tests'/'.tmp'/'websocket';root.mkdir(parents=True,exist_ok=True)
cert=root/'certificate.pem';key=root/'key.pem'
if args.secure:
 config=root/'openssl.cnf';config.write_text('[req]\ndistinguished_name=subject\nx509_extensions=extensions\nprompt=no\n[subject]\nCN=localhost\n[extensions]\nsubjectAltName=DNS:localhost\nbasicConstraints=critical,CA:TRUE\nkeyUsage=critical,digitalSignature,keyEncipherment,keyCertSign\nextendedKeyUsage=serverAuth\n')
 subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','2','-keyout',str(key),'-out',str(cert),'-config',str(config)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
def exact(c,n):
 data=b''
 while len(data)<n:
  part=c.recv(n-len(data))
  if not part:raise EOFError('peer closed')
  data+=part
 return data
def receive(c):
 h=exact(c,2);length=h[1]&127
 if length==126:length=struct.unpack('!H',exact(c,2))[0]
 elif length==127:length=struct.unpack('!Q',exact(c,8))[0]
 mask=exact(c,4) if h[1]&128 else None;data=exact(c,length)
 if mask:data=bytes(b^mask[i%4] for i,b in enumerate(data))
 return h[0]&15,data
def frame(op,data=b'',mask=False):
 h=bytes([0x80|op]);n=len(data)
 if n<126:h+=bytes([n|(128 if mask else 0)])
 elif n<65536:h+=bytes([126|(128 if mask else 0)])+struct.pack('!H',n)
 else:h+=bytes([127|(128 if mask else 0)])+struct.pack('!Q',n)
 if mask:
  mask_key=b'\x12\x34\x56\x78';data=bytes(b^mask_key[i%4] for i,b in enumerate(data));h+=mask_key
 return h+data
def command(script):
 return [executable] if executable else [compiler,str(ROOT/'tests'/script)]
def environment(extra):
 env=os.environ.copy();env.update(extra);return env
errors=[]
def tls_client(c):
 if not args.secure:return c
 context=ssl.create_default_context(cafile=str(cert));return context.wrap_socket(c,server_hostname='localhost')
def tls_server(c):
 if not args.secure:return c
 context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(cert,key);return context.wrap_socket(c,server_side=True)
def python_server(url_box,ready):
 try:
  listener=socket.socket();listener.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);listener.bind(('127.0.0.1',0));listener.listen();listener.settimeout(10)
  scheme='wss' if args.secure else 'ws';url_box.append(f'{scheme}://localhost:{listener.getsockname()[1]}/chat');ready.set()
  c,_=listener.accept();c=tls_server(c);c.settimeout(10);data=b''
  while b'\r\n\r\n' not in data:data+=c.recv(4096)
  client_key=[line.split(b':',1)[1].strip() for line in data.split(b'\r\n') if line.lower().startswith(b'sec-websocket-key:')][0]
  accept=base64.b64encode(hashlib.sha1(client_key+b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest()).decode()
  c.sendall(f'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: {accept}\r\n\r\n'.encode())
  op,msg=receive(c);assert op==1 and msg.decode()=='Привет';c.sendall(frame(1,'echo:Привет'.encode()))
  op,msg=receive(c);assert op==2 and msg==b'abc\0xyz';c.sendall(frame(2,msg))
  op,msg=receive(c);assert op==9 and msg==b'tick';c.sendall(frame(10,msg))
  c.sendall(b'\x01\x05fragm');c.sendall(b'\x80\x0bent-message')
  op,msg=receive(c);assert op==8;c.close();listener.close()
 except Exception as error:errors.append(error);ready.set()
def test_client():
 urls=[];ready=threading.Event();thread=threading.Thread(target=python_server,args=(urls,ready),daemon=True);thread.start();ready.wait()
 if errors:raise errors[0]
 script='websocket_secure_client.lua' if args.secure else 'websocket_client.lua'
 env=environment({'WS_URL':urls[0],'WS_CA':str(cert)})
 result=subprocess.run(command(script),cwd=ROOT,env=env,capture_output=True,text=True,timeout=40)
 thread.join(timeout=2)
 if result.returncode or errors:raise RuntimeError(f'client failed: {result.stdout}\n{result.stderr}\n{errors}')
 assert 'WebSocket client OK' in result.stdout
 print(('WSS' if args.secure else 'WebSocket')+' independent client: OK')
def test_server():
 probe=socket.socket();probe.bind(('127.0.0.1',0));port=probe.getsockname()[1];probe.close()
 script='websocket_secure_server.lua' if args.secure else 'websocket_server.lua'
 env=environment({'WS_PORT':str(port),'WS_CERT':str(cert),'WS_KEY':str(key)})
 process=subprocess.Popen(command(script),cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 c=None
 for _ in range(200):
  try:c=socket.create_connection(('127.0.0.1',port),timeout=.1);break
  except OSError:
   if process.poll() is not None:break
   time.sleep(.03)
 if not c:
  out,err=process.communicate(timeout=5);raise RuntimeError('server did not start: '+out+err)
 c=tls_client(c);c.settimeout(10);client_key=base64.b64encode(os.urandom(16)).decode()
 c.sendall(f'GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {client_key}\r\nSec-WebSocket-Version: 13\r\n\r\n'.encode());data=b''
 while b'\r\n\r\n' not in data:data+=c.recv(4096)
 assert data.startswith(b'HTTP/1.1 101'),data
 c.sendall(frame(1,b'server-text',True));assert receive(c)==(1,b'reply:server-text')
 c.sendall(frame(2,b'a\0b',True));assert receive(c)==(2,b'a\0b')
 c.sendall(frame(9,b'hello',True));assert receive(c)==(10,b'hello')
 c.sendall(b'\x01\x83abcd'+bytes(x^b'abcd'[i%4] for i,x in enumerate(b'one')))
 c.sendall(b'\x80\x84abcd'+bytes(x^b'abcd'[i%4] for i,x in enumerate(b'-two')))
 op,msg=receive(c);assert op==8;c.close();out,err=process.communicate(timeout=40)
 if process.returncode:raise RuntimeError('server failed: '+out+err)
 assert 'WebSocket server OK' in out
 print(('WSS' if args.secure else 'WebSocket')+' independent server: OK')
test_client()
if not args.client_only:test_server()
