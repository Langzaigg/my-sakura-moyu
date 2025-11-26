# -*- coding: utf-8 -*-

import csv, sys, time, zlib
import pandas as pd
import numpy as np
from struct import pack, unpack

def main():
  # rep = list()
  rep = dict()
  pd_source = pd.read_excel('moyu.xlsx',sheet_name=None)
  for sheet_name, data in pd_source.items():
    print(f"Sheet name: {sheet_name}")
    if 'pc' in data.columns:
      i = 0
      for row in data.itertuples():
        if row.pc_line and str(row.pc_line) != 'nan' and row.name_translation != '文字附加符号':
          try:
            o = int(row.pc_line, 16)
            t = str(row.dialogue_translation)
            if sheet_name != 'pc' and t.startswith('「'):
              t=t.replace('「', '').replace('」','')
            rep[o] = t
            i+=1
          except:
            print(row)
          
      print(i)


  source = open('019.csv', "r", encoding="utf-8-sig") # 旧版本csv用于查漏补缺
  reader = csv.reader(source)
  i = 0
  for row in reader:
    o = int(row[1], 16)
    if o not in rep:
      rep[o] = row[3]
      i += 1
  print(i)
  source.close()
  with open("patch.bin.tmp", "wb") as tmp:
    tmp.write(pack("<L", len(rep)))
    for o, t in rep.items():
      tmp.write(pack("<L", o))
      t = t.encode("gbk", errors="ignore") + b"\0"
      tmp.write(pack("<H", len(t)))
      tmp.write(t)
      # else:
        # tmp.write(pack("<L", o))
        # t = b""
        # tmp.write(pack("<H", len(t)))
        # tmp.write(t)
  data = b""
  with open("patch.bin.tmp", "rb") as tmp:
    data = tmp.read()
  target = open("patch.dat", "wb")
  target.write(pack("<L", 0)) # compressed signature
  target.write(pack("<L", int(time.time())))
  target.write(pack("<L", len(data)))
  comp_data = zlib.compress(data)
  target.write(pack("<L", len(comp_data)))
  target.write(comp_data)
  target.close()

if __name__ == "__main__":
  main()
