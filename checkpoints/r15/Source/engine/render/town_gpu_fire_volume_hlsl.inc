// Embedded, versioned with the executable. Float implementation of core
// town_fire_volume.hpp SHA1e87078e...; integer hash and the transport contract
// match, while float noise/transport require their own GPU tolerance gates.
constexpr char FireVolumeShaderSource[] = R"hlsl(
cbuffer FireVolumeField : register(b0) {
 float4 eyeHeightScale, rightBasis, upBasis, forwardBasis;
 float4 cameraData, clipTime, sourceEnergy, config0, config1, context0, context1;
 uint4 roi, control;
 uint4 framePalette[64];
 uint4 toneRamp[64];
};
Texture2D<uint> indexedWorld : register(t0);
Texture2D<float> worldDepth : register(t1);
Texture2D<uint> viewModelCoverage : register(t2);
RWTexture2D<uint> rgbaWorld : register(u0);
RWStructuredBuffer<uint> fieldFailure : register(u1);

uint fireHash(uint v) {
 v ^= v >> 16; v *= 0x7FEB352Du;
 v ^= v >> 15; v *= 0x846CA68Bu;
 return v ^ (v >> 16);
}
float lattice(int3 p, uint identity) {
 uint3 q = (uint3)p & 63u;
 uint v = identity ^ (q.x * 0x9E3779B9u) ^ (q.y * 0x85EBCA6Bu) ^ (q.z * 0xC2B2AE35u);
 return (float)(fireHash(v) >> 8) * (1.0 / 16777216.0);
}
float noise3(float3 p, uint identity) {
 float3 f = floor(p), t = p - f;
 int3 q = (int3)f;
 t = t*t*t*(t*(t*6-15)+10);
 float a = lerp(lerp(lattice(q,identity),lattice(q+int3(1,0,0),identity),t.x),
  lerp(lattice(q+int3(0,1,0),identity),lattice(q+int3(1,1,0),identity),t.x),t.y);
 float b = lerp(lerp(lattice(q+int3(0,0,1),identity),lattice(q+int3(1,0,1),identity),t.x),
  lerp(lattice(q+int3(0,1,1),identity),lattice(q+int3(1,1,1),identity),t.x),t.y);
 return lerp(a,b,t.z);
}
void sampleField(float3 p, out float3 emission, out float sigma) {
 emission = 0; sigma = 0;
 if(context0.y==0 || p.y<0 || p.y>config0.y+config0.z || any(abs(p.xz)>config0.x*1.8)) return;
 float y = p.y/config0.y, fy = y/context0.z;
 float crossAmplitude=0.20*min(y,1.4);
 float swayAx=crossAmplitude*sin(context1.x*17+context1.y+y*3.1);
 float swayAz=crossAmplitude*sin(context1.x*23+context1.y*1.7-y*2.3);
 float swayBx=crossAmplitude*sin(context1.x*19+context1.y*1.3-y*2.7+1.5707963267948966);
 float swayBz=crossAmplitude*sin(context1.x*13+context1.y*0.7+y*2.1+1.5707963267948966);
 float xA=p.x/config0.x-swayAx, zA=p.z/config0.x-swayAz;
 float xB=p.x/config0.x-swayBx, zB=p.z/config0.x-swayBz;
 float x=(xA+xB)*0.5, z=(zA+zB)*0.5;
 float time = clipTime.z;
 uint identity = control.x;
 float warp = noise3(float3(x*1.5,y*2.5-time,z*1.5),identity^0xA511E9B3u)-0.5;
 float n = 0.60*noise3(float3(x*2+warp*0.65,y*4-time*2,z*2-warp*0.65),identity)
  +0.28*noise3(float3(x*4.2,y*8-time*4,z*4.2),identity^0x68E31DA4u)
  +0.12*noise3(float3(x*8.4,y*16-time*6,z*8.4),identity^0xB5297A4Du);
 float flame = 0, heat = 0;
 if(fy<1) {
  float taper = 0.04+0.96*pow(max(0.0,1-fy),0.65);
  float radialA=(xA*xA+zA*zA)/(taper*taper);
  float radialB=(xB*xB+zB*zB)/(taper*taper);
  float vertical=smoothstep(0,0.055,fy)*(1-smoothstep(0.78,1.0,fy))*(1-fy*0.40)*context0.w;
  float flameA=smoothstep(-0.15,0.70,1-radialA+0.50*(n-0.5))*vertical;
  float flameB=smoothstep(-0.15,0.70,1-radialB+0.50*(n-0.5))*vertical;
  float radial=flameB*0.82>flameA?radialB:radialA;
  flame=max(flameA,flameB*0.82);
  heat=saturate((1-saturate(radial))*0.65+(1-fy)*0.35+(n-0.5)*0.12);
 }
 float smoke = 0;
 if(config0.z>0 && y>0.55) {
  float age = (p.y-config0.y*0.55)/(config0.y*0.45+config0.z);
  float radius = 0.42+age*0.70;
  float2 smokeP = float2(x,z)-age*context1.zw;
  float radial = dot(smokeP,smokeP)/(radius*radius);
  smoke = config1.z*smoothstep(0,0.20,age)*(1-smoothstep(0.60,1,age))
   *smoothstep(-0.10,0.80,1-radial+(n-0.5)*0.55)*(1-flame*0.70)*context0.w;
 }
 float sigmaSmoke = config1.y*smoke;
 sigma = config0.w*flame+sigmaSmoke;
 float e = config1.x*flame*context0.y;
 float yellowTip=smoothstep(0.60,0.92,fy);
 float green=(0.26+0.18*heat)*(1-yellowTip)+0.86*yellowTip;
 float blue=(0.008+0.018*heat)*(1-yellowTip)+0.10*yellowTip;
 emission=float3(e+sigmaSmoke*0.012,e*green+sigmaSmoke*0.010,e*blue+sigmaSmoke*0.008);
}
bool intersectBox(float3 o,float3 d,float minT,float maxT,out float entry,out float exit) {
 entry=minT; exit=min(maxT,4096.0);
 float extent=config0.x*1.8;
 float3 lo=float3(-extent,0,-extent), hi=float3(extent,config0.y+config0.z,extent);
 [unroll] for(uint axis=0;axis<3;++axis) {
  if(abs(d[axis])<1e-8) { if(o[axis]<lo[axis] || o[axis]>hi[axis]) return false; }
  else {
   float a=(lo[axis]-o[axis])/d[axis], b=(hi[axis]-o[axis])/d[axis];
   entry=max(entry,min(a,b)); exit=min(exit,max(a,b));
   if(entry>=exit) return false;
  }
 }
 return entry<exit;
}
float3 decodeRgb(uint p) { return float3((p>>16)&255,(p>>8)&255,p&255)/255.0; }
float3 toLinear(float3 s) {
 return float3(s.x<=0.04045?s.x/12.92:pow((s.x+0.055)/1.055,2.4),
  s.y<=0.04045?s.y/12.92:pow((s.y+0.055)/1.055,2.4),s.z<=0.04045?s.z/12.92:pow((s.z+0.055)/1.055,2.4));
}
float3 toSrgb(float3 l) {
 return float3(l.x<=0.0031308?l.x*12.92:1.055*pow(l.x,1.0/2.4)-0.055,
  l.y<=0.0031308?l.y*12.92:1.055*pow(l.y,1.0/2.4)-0.055,l.z<=0.0031308?l.z*12.92:1.055*pow(l.z,1.0/2.4)-0.055);
}
void failRay(uint stageBit) { uint ignored; InterlockedOr(fieldFailure[0],stageBit,ignored); }

[numthreads(8,8,1)]
void CS(uint3 thread : SV_DispatchThreadID) {
 if(thread.x>=roi.z || thread.y>=roi.w) return;
 uint2 at=roi.xy+thread.xy;
 if(any(at>=control.yz) || viewModelCoverage.Load(int3(at,0))!=0 || sourceEnergy.w==0) return;
 float2 screen=(float2(at)+0.5)/cameraData.w;
 float vx=(screen.x-cameraData.y)/cameraData.x, vy=(cameraData.z-screen.y)/cameraData.x;
 float3 rayOrigin=eyeHeightScale.xyz, rayDirection=forwardBasis.xyz;
 if(clipTime.w!=0) rayDirection+=rightBasis.xyz*vx+upBasis.xyz*vy;
 else {
  float3 displacement=rightBasis.xyz*vx+upBasis.xyz*vy;
  displacement.y/=eyeHeightScale.w; rayOrigin+=displacement;
 }
 rayDirection.y/=eyeHeightScale.w;
 float lengthSquared=dot(rayDirection,rayDirection);
 if(!isfinite(lengthSquared) || lengthSquared<=0) { failRay(1u); return; }
 rayDirection*=rsqrt(lengthSquared);
 float3 scaledOrigin=(rayOrigin-eyeHeightScale.xyz)*float3(1,eyeHeightScale.w,1);
 float originDepth=dot(scaledOrigin,forwardBasis.xyz);
 float depthPerTile=dot(rayDirection*float3(1,eyeHeightScale.w,1),forwardBasis.xyz);
 float opaque=worldDepth.Load(int3(at,0));
 if(isnan(opaque) || opaque<0 || !isfinite(depthPerTile) || depthPerTile<=0) { failRay(1u); return; }
 float minT=max(0.0,(clipTime.x-originDepth)/depthPerTile);
 float maxT=(min(opaque,clipTime.y)-originDepth)/depthPerTile;
 if(!isfinite(minT) || !isfinite(maxT)) { failRay(1u); return; }
 if(maxT<=minT) return;
 float3 localOrigin=rayOrigin-sourceEnergy.xyz;
 if(any(abs(localOrigin)>4096.0) || minT>4096.0) { failRay(1u); return; }
 float entry,exit;
 if(!intersectBox(localOrigin,rayDirection,minT,maxT,entry,exit)) return;
 float step=(exit-entry)/(float)control.w, transmittance=1;
 float3 radiance=0;
 [loop] for(uint i=0;i<control.w;++i) {
  float distance=entry+((float)i+0.5)*step;
  float3 emission; float sigma;
  sampleField(localOrigin+rayDirection*distance,emission,sigma);
  // maxT is the actual opaque endpoint BEFORE the AABB clip.
  float intersection=config1.w>0?smoothstep(0,config1.w,maxT-distance):1;
  sigma*=intersection;
  float tau=sigma*step;
  // Stable -expm1(-tau), including vanishing absorption/additive emission.
  float opacity=tau<1e-3?tau*(1-tau*(0.5-tau/6)):(1-exp(-tau));
  float factor=transmittance*intersection*(sigma>1e-12?opacity/sigma:step);
  radiance+=emission*factor;
  transmittance*=1-opacity;
 }
 if(any(!isfinite(radiance)) || !isfinite(transmittance)) { failRay(1u); return; }
 float alpha=saturate(1-transmittance);
 if(alpha==0 && all(radiance==0)) return;
 uint base=rgbaWorld[at];
 if(base==0) { uint index=min(indexedWorld.Load(int3(at,0)),255u); base=framePalette[index>>2][index&3u]; }
 float3 emissionEncoded=toSrgb(max(radiance,0));
 uint3 rampIndex=(uint3)(saturate(emissionEncoded)*255.0+0.5);
 uint red=toneRamp[rampIndex.r>>2][rampIndex.r&3u];
 uint green=toneRamp[rampIndex.g>>2][rampIndex.g&3u];
 uint blue=toneRamp[rampIndex.b>>2][rampIndex.b&3u];
 float3 emission=toLinear(float3((red>>16)&255,(green>>8)&255,blue&255)/255.0);
 float3 color=toSrgb(emission+toLinear(decodeRgb(base))*(1-alpha));
 if(any(!isfinite(color))) { failRay(1u); return; }
 uint3 c=(uint3)(saturate(color)*255.0+0.5);
 rgbaWorld[at]=(base&0xFF000000u)|(c.r<<16)|(c.g<<8)|c.b;
}
// Adaptive R2 is a different full-ray quadrature. CS above remains the exact
// Direct CS retains the R14 24-step reference path. The host sends control.w=24
// without packed stride to CS; CSField/CSComposite use low8=12 plus packed stride.
// One reused adaptive source field has 65536 * 14 float4 records (14 MiB). The host
// admits the aggregate source-ray budget before omission and unbinds u2
// before t3. Only failRay's existing atomic error mask is shared by threads.
RWStructuredBuffer<float4> firePrefixWrite : register(u2);
StructuredBuffer<float4> firePrefixRead : register(t3);

bool adaptivePlan(out uint stride, out uint2 grid) {
 stride=(control.w>>8)&65535u; if(stride==0) stride=1;
 grid=0;
 if((control.w&255u)!=12u || (control.w>>24)!=0u || control.y==0 || control.z==0
  || roi.z==0 || roi.w==0 || roi.z>16384u || roi.w>16384u || roi.x>=control.y || roi.y>=control.z
  || roi.z>control.y-roi.x || roi.w>control.z-roi.y) return false;
 grid=uint2((roi.z-1u)/stride+1u,(roi.w-1u)/stride+1u);
 // Division avoids overflow even for malformed dimensions.
 return grid.x!=0 && grid.y!=0 && grid.x<=65536u && grid.y<=65536u/grid.x;
}
float adaptiveCellCenter(uint index, uint extent, uint stride) {
 uint offset=index*stride;
 return (float)offset+0.5*(float)min(stride,extent-offset);
}
bool adaptiveAxis(float center, uint extent, uint stride, uint count,
 out uint lower, out uint upper, out float weight) {
 lower=upper=0; weight=0;
 if(!isfinite(center) || extent==0 || extent>16384u || stride==0 || stride>65535u
  || count==0 || count!=(extent-1u)/stride+1u) return false;
 float first=adaptiveCellCenter(0,extent,stride);
 float last=adaptiveCellCenter(count-1u,extent,stride);
 if(center<=first) return true;
 if(center>=last) { lower=upper=count-1u; return true; }
 uint lo=0,hi=count-1u;
 [loop] while(hi-lo>1u) {
  uint middle=lo+(hi-lo)/2u;
  if(center<adaptiveCellCenter(middle,extent,stride)) hi=middle;
  else lo=middle;
 }
 float a=adaptiveCellCenter(lo,extent,stride),b=adaptiveCellCenter(hi,extent,stride);
 if(!isfinite(a) || !isfinite(b) || b<=a) return false;
 lower=lo; upper=hi; weight=(center-a)/(b-a);
 return isfinite(weight) && weight>=0 && weight<=1;
}
bool adaptiveRay(float2 rasterCenter, out float3 origin, out float3 direction,
 out float originDepth, out float depthPerTile) {
 origin=direction=0; originDepth=depthPerTile=0;
 if(any(!isfinite(rasterCenter)) || any(!isfinite(eyeHeightScale))
  || any(!isfinite(cameraData)) || any(!isfinite(clipTime))
  || cameraData.x<=0 || cameraData.w<1 || cameraData.w>4 || floor(cameraData.w)!=cameraData.w
  || eyeHeightScale.w<=0 || clipTime.x<0 || clipTime.y<=clipTime.x) return false;
 // rasterCenter already denotes the true cell/pixel center. No second +.5.
 float2 screen=rasterCenter/cameraData.w;
 float vx=(screen.x-cameraData.y)/cameraData.x,vy=(cameraData.z-screen.y)/cameraData.x;
 origin=eyeHeightScale.xyz; direction=forwardBasis.xyz;
 if(clipTime.w!=0) direction+=rightBasis.xyz*vx+upBasis.xyz*vy;
 else {
  float3 displacement=rightBasis.xyz*vx+upBasis.xyz*vy;
  displacement.y/=eyeHeightScale.w; origin+=displacement;
 }
 direction.y/=eyeHeightScale.w;
 float lengthSquared=dot(direction,direction);
 if(!isfinite(lengthSquared) || lengthSquared<=0 || any(!isfinite(origin))) return false;
 direction*=rsqrt(lengthSquared);
 originDepth=dot((origin-eyeHeightScale.xyz)*float3(1,eyeHeightScale.w,1),forwardBasis.xyz);
 depthPerTile=dot(direction*float3(1,eyeHeightScale.w,1),forwardBasis.xyz);
 return all(isfinite(direction)) && isfinite(originDepth) && isfinite(depthPerTile) && depthPerTile>0;
}
bool adaptivePrefixValid(float4 p) {
 return all(isfinite(p)) && all(p.xyz>=0) && p.w>=0 && p.w<=1;
}
// A positive spatial ray interval can collapse to one representable camera
// depth when a coarse sample grazes the volume. Preserve that integrated ray
// as a depth step; still reject non-finite or reversed intervals.
bool adaptiveDepthIntervalValid(float entryDepth,float exitDepth) {
 return isfinite(entryDepth) && isfinite(exitDepth) && exitDepth>=entryDepth;
}

[numthreads(8,8,1)]
void CSField(uint3 thread : SV_DispatchThreadID) {
 uint stride; uint2 grid;
 if(!adaptivePlan(stride,grid)) { failRay(2u); return; }
 if(thread.x>=grid.x || thread.y>=grid.y) return;
 uint records,recordStride; firePrefixWrite.GetDimensions(records,recordStride);
 uint rays=grid.x*grid.y;
 if(recordStride!=16u || records<rays*14u) { failRay(2u); return; }
 uint start=(thread.y*grid.x+thread.x)*14u;
 // Initialize EVERY logical ray, including valid empty/outside rays. A reused
 // buffer can never expose a previous source's prefix after an early return.
 firePrefixWrite[start]=0;
 [unroll] for(uint k=0;k<13u;++k) firePrefixWrite[start+1u+k]=float4(0,0,0,1);
 if(!isfinite(sourceEnergy.w) || sourceEnergy.w<0) { failRay(2u); return; }
 if(sourceEnergy.w==0) return;
 float2 center=float2(roi.xy)+float2(adaptiveCellCenter(thread.x,roi.z,stride),adaptiveCellCenter(thread.y,roi.w,stride));
 if(any(center<0) || any(center>=float2(control.yz))) return;
 float3 origin,direction; float originDepth,axial;
 if(!adaptiveRay(center,origin,direction,originDepth,axial)) { failRay(2u); return; }
 float minT=max(0.0,(clipTime.x-originDepth)/axial),maxT=(clipTime.y-originDepth)/axial;
 float3 local=origin-sourceEnergy.xyz;
 if(any(!isfinite(local)) || any(abs(local)>4096.0) || !isfinite(minT) || !isfinite(maxT) || minT>4096.0) { failRay(2u); return; }
 if(maxT<=minT) return;
 float entry,exit;
 // Field transport deliberately ignores opaque depth and viewmodel coverage.
 if(!intersectBox(local,direction,minT,maxT,entry,exit)) return;
 float entryDepth=originDepth+entry*axial,exitDepth=originDepth+exit*axial;
 float step=(exit-entry)/12.0;
 if(!adaptiveDepthIntervalValid(entryDepth,exitDepth) || !isfinite(step) || step<=0) { failRay(2u); return; }
 float3 radiance=0; float transmittance=1;
 [loop] for(uint i=0;i<12u;++i) {
  float distance=entry+((float)i+0.5)*step;
  float3 emission; float sigma;
  sampleField(local+direction*distance,emission,sigma);
  if(any(!isfinite(emission)) || any(emission<0) || !isfinite(sigma) || sigma<0) { failRay(2u); return; }
  float tau=sigma*step;
  if(!isfinite(tau) || tau<0) { failRay(2u); return; }
  float opacity=tau<1e-3?tau*(1-tau*(0.5-tau/6)):(1-exp(-tau));
  float factor=transmittance*(sigma>1e-12?opacity/sigma:step);
  float3 nextRadiance=radiance+emission*factor;
  float nextTransmittance=transmittance*(1-opacity);
  if(!isfinite(factor) || factor<0 || any(!isfinite(nextRadiance)) || any(nextRadiance<radiance)
   || !isfinite(nextTransmittance) || nextTransmittance<0 || nextTransmittance>transmittance) { failRay(2u); return; }
  radiance=nextRadiance; transmittance=nextTransmittance;
  firePrefixWrite[start+2u+i]=float4(radiance,transmittance);
 }
 // Publication marker is written only after every boundary is complete.
 firePrefixWrite[start]=float4(entryDepth,exitDepth,1,0);
}

bool adaptiveAtDepth(uint rayIndex,float cutoff,out float4 transport) {
 transport=float4(0,0,0,1);
 uint start=rayIndex*14u;
 float4 meta=firePrefixRead[start];
 if(any(!isfinite(meta)) || (meta.z!=0 && meta.z!=1) || meta.w!=0) return false;
 if(meta.z==0) return true; // Valid empty ray keeps its spatial weight.
 if(!adaptiveDepthIntervalValid(meta.x,meta.y) || !isfinite(cutoff)) return false;
 if(meta.y==meta.x) {
  if(cutoff<=meta.x) return true;
  transport=firePrefixRead[start+13u];
  return adaptivePrefixValid(transport);
 }
 if(cutoff<=meta.x) return true;
 float location=clamp((cutoff-meta.x)/(meta.y-meta.x)*12.0,0.0,12.0);
 if(!isfinite(location)) return false;
 if(location>=12) {
  transport=firePrefixRead[start+13u];
  return adaptivePrefixValid(transport);
 }
 uint segment=min((uint)floor(location),11u); float fraction=location-(float)segment;
 float4 a=firePrefixRead[start+1u+segment],b=firePrefixRead[start+2u+segment];
 if(!adaptivePrefixValid(a) || !adaptivePrefixValid(b) || any(b.xyz<a.xyz) || b.w>a.w) return false;
 if(fraction<=0) { transport=a; return true; }
 if(fraction>=1) { transport=b; return true; }
 // Approximation, not a zero-energy claim: for the host-admitted field bounds,
 // residual T<=1e-8 bounds all remaining radiance by 6.96e-5 linear (<.23 of
 // one sRGB8 code before quantization). Final rounding can still move one code.
 // Retain the preceding boundary rather than exposing any future full knot.
 if(a.w<=1e-8) { transport=a; return true; }
 // Prefix validation above proves 0<=b.w<=a.w and this path proves a.w>1e-8.
 // Derive a bounded loss before reconstructing the endpoint ratio. Direct
 // b.w/a.w can be rounded above1 by approximate GPU division even though the
 // stored prefix is monotone. Equal endpoints produce exactly q=1; a zero
 // endpoint remains fatal and is outside the validated default C1 tau bound.
 precise float relativeLoss=(a.w-b.w)/a.w;
 if(!isfinite(relativeLoss) || relativeLoss<0 || relativeLoss>1) return false;
 precise float q=1.0-relativeLoss;
 if(!isfinite(q) || q<=0 || q>1) return false;
 float attenuation,weight;
 if(1-q<=1e-6) { attenuation=pow(q,fraction); weight=fraction; }
 else {
  attenuation=pow(q,fraction);
  // Stable partial opacity. Direct subtraction loses small segments when q^f
  // rounds near1; this series is the same -expm1 approximation as CS.
  float partialTau=-log(q)*fraction;
  if(!isfinite(partialTau) || partialTau<0) return false;
  float partialOpacity=partialTau<1e-3?partialTau*(1-partialTau*(0.5-partialTau/6)):(1-exp(-partialTau));
  weight=partialOpacity/(1-q);
 }
 if(!isfinite(attenuation) || !isfinite(weight) || attenuation<0 || attenuation>1
  || weight<0 || weight>1.000001) return false;
 weight=min(weight,1.0);
 float3 reconstructed=a.xyz+(b.xyz-a.xyz)*weight;
 float remaining=a.w*attenuation;
 // Bound only floating reconstruction roundoff (four float epsilons), then
 // constrain the result to the two published boundaries. Larger drift fails.
 float3 energyTolerance=4.76837158203125e-7*max(abs(b.xyz),float3(1e-20,1e-20,1e-20));
 float transmissionTolerance=4.76837158203125e-7*max(a.w,1e-20);
 if(any(!isfinite(reconstructed)) || !isfinite(remaining)
  || any(reconstructed<a.xyz-energyTolerance) || any(reconstructed>b.xyz+energyTolerance)
  || remaining<b.w-transmissionTolerance || remaining>a.w+transmissionTolerance) return false;
 transport=float4(clamp(reconstructed,a.xyz,b.xyz),clamp(remaining,b.w,a.w));
 return adaptivePrefixValid(transport);
}

[numthreads(8,8,1)]
void CSComposite(uint3 thread : SV_DispatchThreadID) {
 uint stride; uint2 grid;
 if(!adaptivePlan(stride,grid)) { failRay(4u); return; }
 if(thread.x>=roi.z || thread.y>=roi.w) return;
 uint2 at=roi.xy+thread.xy;
 if(any(at>=control.yz) || viewModelCoverage.Load(int3(at,0))!=0 || sourceEnergy.w==0) return;
 uint records,recordStride; firePrefixRead.GetDimensions(records,recordStride);
 if(recordStride!=16u || records<grid.x*grid.y*14u) { failRay(4u); return; }
 float3 origin,direction; float originDepth,axial;
 if(!adaptiveRay(float2(at)+0.5,origin,direction,originDepth,axial)) { failRay(4u); return; }
 float opaque=worldDepth.Load(int3(at,0));
 if(isnan(opaque) || opaque<0) { failRay(4u); return; }
 float cutoff=min(opaque,clipTime.y);
 float minT=max(0.0,(clipTime.x-originDepth)/axial),maxT=(cutoff-originDepth)/axial;
 if(!isfinite(minT) || !isfinite(maxT)) { failRay(4u); return; }
 if(maxT<=minT) return;
 float3 local=origin-sourceEnergy.xyz;
 if(any(!isfinite(local)) || any(abs(local)>4096.0) || minT>4096.0) { failRay(4u); return; }
 float entry,exit;
 if(!intersectBox(local,direction,minT,maxT,entry,exit)) return;
 uint left,right,top,bottom; float wx,wy;
 if(!adaptiveAxis((float)thread.x+0.5,roi.z,stride,grid.x,left,right,wx)
  || !adaptiveAxis((float)thread.y+0.5,roi.w,stride,grid.y,top,bottom,wy)) { failRay(4u); return; }
 float4 a,b,c,d;
 // Every neighbor is clipped to the SAME actual fine-pixel axial receiver
 // depth BEFORE spatial blending. No neighbor's whole future integral leaks.
 if(!adaptiveAtDepth(top*grid.x+left,cutoff,a) || !adaptiveAtDepth(top*grid.x+right,cutoff,b)
  || !adaptiveAtDepth(bottom*grid.x+left,cutoff,c) || !adaptiveAtDepth(bottom*grid.x+right,cutoff,d)) { failRay(4u); return; }
 if(all(a==float4(0,0,0,1)) && all(b==a) && all(c==a) && all(d==a)) return;
 float4 transport=a*((1-wx)*(1-wy))+b*(wx*(1-wy))+c*((1-wx)*wy)+d*(wx*wy);
 if(any(!isfinite(transport)) || any(transport.xyz<0) || transport.w<0 || transport.w>1.000001) { failRay(4u); return; }
 transport.w=min(transport.w,1.0);
 float3 radiance=transport.xyz; float alpha=saturate(1-transport.w);
 if(alpha==0 && all(radiance==0)) return;
 // Same tone/palette/sRGB/background-alpha composition as the direct R1 CS.
 uint base=rgbaWorld[at];
 if(base==0) { uint index=min(indexedWorld.Load(int3(at,0)),255u); base=framePalette[index>>2][index&3u]; }
 float3 emissionEncoded=toSrgb(max(radiance,0));
 uint3 rampIndex=(uint3)(saturate(emissionEncoded)*255.0+0.5);
 uint red=toneRamp[rampIndex.r>>2][rampIndex.r&3u];
 uint green=toneRamp[rampIndex.g>>2][rampIndex.g&3u];
 uint blue=toneRamp[rampIndex.b>>2][rampIndex.b&3u];
 float3 emission=toLinear(float3((red>>16)&255,(green>>8)&255,blue&255)/255.0);
 float3 color=toSrgb(emission+toLinear(decodeRgb(base))*(1-alpha));
 if(any(!isfinite(color))) { failRay(4u); return; }
 uint3 value=(uint3)(saturate(color)*255.0+0.5);
 rgbaWorld[at]=(base&0xFF000000u)|(value.r<<16)|(value.g<<8)|value.b;
}
)hlsl";
