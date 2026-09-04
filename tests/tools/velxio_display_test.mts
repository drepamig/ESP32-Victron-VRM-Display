import assert from 'node:assert/strict';
import { Display } from '../../tools/velxio/display.mts';
const lcd = new Display();
function cmd(c, data = []) {
  lcd.event({type:'gpio_change', pin:2, state:0});
  lcd.event({type:'spi_batch', b64:Buffer.from([c]).toString('base64')});
  lcd.event({type:'gpio_change', pin:2, state:1});
  lcd.event({type:'spi_batch', b64:Buffer.from(data).toString('base64')});
}
cmd(0x2a,[0,0,0,1]); cmd(0x2b,[0,0,0,0]);
cmd(0x2c,[0xf8,0,0x07,0xe0]);
assert.deepEqual([...lcd.native().slice(0,8)], [248,0,0,255,0,252,0,255]);
cmd(0x21); cmd(0x2c,[0,0]);
assert.deepEqual([...lcd.native().slice(0,4)], [248,252,248,255], 'RAMWR starts again at window origin with RGB565 inversion');
assert.equal(lcd.landscape().length,320*240*4);
lcd.close();
