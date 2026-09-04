import fs from 'node:fs';
import path from 'node:path';
import readline from 'node:readline';
import { Display } from './display.mts';
const dir=process.argv[2];
let lcd=new Display();
try {
  for await(const line of readline.createInterface({input:fs.createReadStream(path.join(dir,'events.jsonl')),crlfDelay:Infinity})) {
    const ev=JSON.parse(line);
    if(ev.type==='bench_restart') {lcd.close();lcd=new Display();}
    lcd.event(ev);
    if(ev.type==='bench_capture') {
      if(!/^[a-z0-9-]+$/.test(ev.name)) throw new Error('unsafe checkpoint');
      fs.writeFileSync(path.join(dir,ev.name+'.rgba'),lcd.landscape());
    }
  }
} finally {lcd.close();}
