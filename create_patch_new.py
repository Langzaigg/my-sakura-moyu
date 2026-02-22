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
    if 'pc_1_1' in data.columns:
      i = 0
      for row in data.itertuples():
        if row.pc_1_1 and row.dialogue_translation and str(row.pc_1_1) != 'nan' and str(row.dialogue_translation) != 'nan':
          try:
            o = int(row.pc_1_1, 16)
            t = str(row.dialogue_translation).replace('[・|','[·|').replace('[Ｋａｎａｄｅ|奏][Ｔａｉｇａ|大雅]','[Kanate|奏] [Taiga|大雅]').replace('ｔａｉｇａ','Taiga')
            if sheet_name != 'pc' and t.startswith('「'):
              t=t.replace('「', '').replace('」','')
            rep[o] = t
            i+=1
          except:
            print(row)
      print(i)

  print('Sakuramoyu-v1.1.csv')
  source = open('Sakuramoyu-v1.1.csv', "r", encoding="utf-8-sig")
  reader = csv.reader(source)
  i = 0
  for row in reader:
    o = int(row[0], 16)
    if o not in rep:
      rep[o] = row[2]
      i += 1
      # print(row[0], row[2])
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
