"""N16 original math ctor's1000-scale angle table; not the motion Q15 table.
The source FPU initializer is reviewed, not executed by this generator.
"""
import math
from pathlib import Path
def table():
    values=[0]*101;values[100]=45;index=0;previous=0
    for degree in range(46):
        theta=degree*3.14159265359/180
        limit=int(abs(math.sin(theta))*1000)*100//int(abs(math.cos(theta))*1000)
        while index<limit:values[index]=previous;index+=1
        previous=degree
    return values
if __name__=='__main__':
    target=Path(__file__).resolve().parents[1]/'core/ai_atan.inc'
    target.write_text('/* Derived from PKR10C8:0000/PKE1058:0000, scale1000. */\n'+','.join(map(str,table()))+'\n','ascii')
