#ifndef _SerialEEPROM_h
#define _SerialEEPROM_h

#include <Wire.h>

class SerialEEPROM
{
  public:
  SerialEEPROM() : address(0), capacity(0) { }

  void init(uint8_t address_, size_t capacity_)
  {
    address = address_;
    capacity = capacity_;
    Wire.begin();  
  }

  void write(uint16_t & offset, void * data_, size_t quantity, bool update = false) 
  {
    byte * data = (byte *) data_;
    size_t count = 0;
    byte tmp[64];
    // calculate how much space is left in the first page
    size_t chunk = (((offset >> 6) + 1) << 6) - offset; // shifting 6 is x/64
    if(chunk > quantity) chunk = quantity;
    while(quantity) {
      size_t last = count + chunk;
      uint16_t o = offset;
      if(!update || (read(o,tmp,chunk) != chunk) || memcmp(tmp,data+count,chunk)) {
        Wire.beginTransmission(address);
        Wire.write((uint8_t)(offset >> 8));
        Wire.write((uint8_t)(offset));
        while(count < last) Wire.write(data[count++]);
        Wire.endTransmission();
        delay(6); // use 6 instead of 5 for 20MHz ?
      } else {
        count =  last;
      }
      quantity -= chunk;
      offset += chunk;
      chunk = 64;
      if(chunk > quantity) chunk = quantity;
    }
  }

  void update(uint16_t & offset, void * data, size_t quantity) 
  {
    write(offset,data,quantity,true);
  }
/*
  template<class T>
  void write(uint16_t & offset, T & data)
  {
    write(offset,(void*)&data,sizeof(data),false); 
  }

  template<class T>
  void update(uint16_t & offset, T & data)
  {
    write(offset,(void*)&data,sizeof(data),true); 
  }
*/
  size_t read(uint16_t & offset, void * data_, size_t quantity) 
  {
    byte * data = (byte *) data_;
    Wire.beginTransmission(address);
    Wire.write((uint8_t)(offset >> 8));
    Wire.write((uint8_t)(offset));
    Wire.endTransmission();
    Wire.requestFrom(address,quantity);
    size_t count = 0;
    while(Wire.available()) data[count++] = Wire.read();
    offset += quantity;
    return count;
  }
/*
  template<class T>
  size_t read(uint16_t & offset, T & data)
  {
    return read(offset,(void*)&data,sizeof(data)); 
  }
*/ 
  uint8_t address;
  size_t  capacity;
};

#endif
