// Hyllian xBR-lv2, CORNER_C / smooth tips, fixed 3x grid.
// Copyright (C) 2011-2016 Hyllian. MIT: third_party/xbr-LICENSE.txt.
static constexpr char xbr_fragment[]=R"(
uniform sampler2D picture;
uniform vec2 pictureSize;
#ifdef GL_ES
#ifndef GL_FRAGMENT_PRECISION_HIGH
#error Optional xBR GPU path needs highp; use CPU fallback
#endif
varying highp vec2 texcoord;
#else
varying vec2 texcoord;
#endif
vec4 sampleColor(vec2 base,vec2 offset){
 vec4 c=floor(texture2D(picture,base+offset/pictureSize)*255.0+0.5);if(c.a<255.0)c.rgb*=c.a/255.0;return c;
}
float value(vec4 c){return 897.0*c.r+1761.0*c.g+342.0*c.b;}
vec4 diff(vec4 a,vec4 b){return vec4(notEqual(a,b));}
vec4 eq(vec4 a,vec4 b){return step(abs(a-b),vec4(239062.5));}
vec4 neq(vec4 a,vec4 b){return vec4(1.0)-eq(a,b);}
float distanceColor(vec4 a,vec4 b){vec4 d=abs(a-b);return d.x+d.y+d.z+d.w;}
void main(){
 vec2 grid=floor(texcoord*pictureSize*3.0);
 vec2 fp=(mod(grid,3.0)+0.5)/3.0,base=(floor(grid/3.0)+0.5)/pictureSize;
 vec4 A1=sampleColor(base,vec2(-1.0,-2.0)),B1=sampleColor(base,vec2(0.0,-2.0)),C1=sampleColor(base,vec2(1.0,-2.0));
 vec4 A=sampleColor(base,vec2(-1.0,-1.0)),B=sampleColor(base,vec2(0.0,-1.0)),C=sampleColor(base,vec2(1.0,-1.0));
 vec4 D=sampleColor(base,vec2(-1.0,0.0)),E=sampleColor(base,vec2(0.0)),F=sampleColor(base,vec2(1.0,0.0));
 vec4 G=sampleColor(base,vec2(-1.0,1.0)),H=sampleColor(base,vec2(0.0,1.0)),I=sampleColor(base,vec2(1.0,1.0));
 vec4 G5=sampleColor(base,vec2(-1.0,2.0)),H5=sampleColor(base,vec2(0.0,2.0)),I5=sampleColor(base,vec2(1.0,2.0));
 vec4 A0=sampleColor(base,vec2(-2.0,-1.0)),D0=sampleColor(base,vec2(-2.0,0.0)),G0=sampleColor(base,vec2(-2.0,1.0));
 vec4 C4=sampleColor(base,vec2(2.0,-1.0)),F4=sampleColor(base,vec2(2.0,0.0)),I4=sampleColor(base,vec2(2.0,1.0));
 vec4 b=vec4(value(B),value(D),value(H),value(F));
 vec4 c=vec4(value(C),value(A),value(G),value(I));
 vec4 d=b.yzwx,e=vec4(value(E)),f=b.wxyz,g=c.zwxy,h=b.zwxy,i=c.wxyz;
 vec4 i4=vec4(value(I4),value(C1),value(A0),value(G5));
 vec4 i5=vec4(value(I5),value(C4),value(A1),value(G0));
 vec4 h5=vec4(value(H5),value(F4),value(B1),value(D0)),f4=h5.yzwx;
 vec4 initial=diff(e,f)*diff(e,h);
 vec4 corner=initial*(neq(f,b)*neq(f,c)+neq(h,d)*neq(h,g)+eq(e,i)*(neq(f,f4)*neq(f,i4)+neq(h,h5)*neq(h,i5))+eq(e,g)+eq(e,c));
 vec4 wd1=abs(e-c)+abs(e-g)+abs(i-h5)+abs(i-f4)+4.0*abs(h-f);
 vec4 wd2=abs(h-d)+abs(h-i5)+abs(f-i4)+abs(f-b)+4.0*abs(e-i);
 vec4 inner=step(wd1,wd2)*initial;
 vec4 edge=step(wd1+1593.75,wd2)*step(vec4(0.5),corner);
 vec4 left=step(2.0*abs(f-g),abs(h-c))*diff(e,g)*diff(d,g)*edge;
 vec4 up=step(2.0*abs(h-c),abs(f-g))*diff(e,c)*diff(b,c)*edge;
 vec4 fx=vec4(1.0,-1.0,-1.0,1.0)*fp.y+vec4(1.0,1.0,-1.0,-1.0)*fp.x;
 vec4 fl=vec4(1.0,-1.0,-1.0,1.0)*fp.y+vec4(0.5,2.0,-0.5,-2.0)*fp.x;
 vec4 fu=vec4(1.0,-1.0,-1.0,1.0)*fp.y+vec4(2.0,0.5,-2.0,-0.5)*fp.x;
 vec4 delta=vec4(1.0/3.0),dl=vec4(1.0/6.0,1.0/3.0,1.0/6.0,1.0/3.0),du=dl.yxwz;
 vec4 co=vec4(1.5,0.5,-0.5,0.5);
 vec4 weights=max(max(edge*clamp((fx+delta-co)/(2.0*delta),0.0,1.0),inner*clamp((fx+delta-co-0.25)/(2.0*delta),0.0,1.0)),
                  max(left*clamp((fl+dl-vec4(1.0,1.0,-0.5,0.0))/(2.0*dl),0.0,1.0),up*clamp((fu+du-vec4(2.0,0.0,-1.0,0.5))/(2.0*du),0.0,1.0)));
 // Exact weights on the fixed 3x grid are multiples of 1/8. Snap away
 // interpolation error before the equal-distance contour tie-break.
 weights=floor(weights*8.0+0.5)/8.0;
 vec4 choose=step(abs(e-f),abs(e-h));
 vec4 a=mix(mix(E,mix(H,F,choose.x),weights.x),mix(B,D,choose.z),weights.z);
 vec4 z=mix(mix(E,mix(F,B,choose.y),weights.y),mix(D,H,choose.w),weights.w);
 vec4 result=distanceColor(E,a)<=distanceColor(E,z)?z:a;
 if(result.a>0.0)result.rgb*=255.0/result.a;
 gl_FragColor=result/255.0;
}
)";
