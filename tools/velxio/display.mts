import { ili9341Simulation } from './ili9341.mts';
globalThis.window = globalThis;

// Keep the decoder's actual panel buffer; no resampling or color normalization.
export class Display {
  buffer = null;
  listeners = new Map();
  spi = {onByte:null};
  cleanup;
  constructor() {
    const ctx = {
      createImageData:(w,h) => { const frame={data:new Uint8ClampedArray(w*h*4)}; this.buffer=frame.data; return frame; },
      putImageData:() => {},
      clearRect:() => {this.buffer=null;},
    };
    const element={canvas:{getContext:()=>ctx},addEventListener:()=>{},removeEventListener:()=>{}};
    const simulator={pinManager:{onPinChange:(pin,fn)=>{this.listeners.set(pin,fn);return ()=>this.listeners.delete(pin);}},spi:this.spi};
    this.cleanup=ili9341Simulation.attachEvents(element,simulator,pin=>pin==='D/C'?2:null);
  }
  event(ev) {
    if(ev.type==='gpio_change') this.listeners.get(ev.pin)?.(ev.pin,!!ev.state);
    if(ev.type==='spi_batch') for(const byte of Buffer.from(ev.b64,'base64')) this.spi.onByte(byte);
  }
  native() {
    if(!this.buffer) throw new Error('no captured display pixels');
    return this.buffer;
  }
  landscape() {
    const native=this.native();
    const result=new Uint8ClampedArray(320*240*4);
    for(let y=0;y<240;y++) for(let x=0;x<320;x++) {
      const src=((319-x)*240+y)*4;
      result.set(native.subarray(src,src+4),(y*320+x)*4);
    }
    return result;
  }
  close() { this.cleanup(); }
}
