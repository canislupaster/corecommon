R"(precision mediump float;

in vec2 fragtexpos;

uniform vec2 resolution;
uniform vec2 mouse;

layout(location=0) out vec4 outColor;

float func(float x, float y) {
    //return x*x+y*y;
    x /= 4.0;
    y /= 4.0;
    return 4.0*x*y-pow(x,4.0)-pow(y,4.0)+4.0;
    //return sin(x)+cos(y);
}


float intersection(vec3 ro, vec3 rd, float t) {
    return ro.y+rd.y*t-func(ro.x+rd.x*t, ro.z+rd.z*t);
}

const int num_cast = 8;
const int num_level = 4;
const float level_min = 1;
const float level_max = 30;

float level_intersection(vec3 ro, vec3 rd, float t) {
    float outy = 1.0;
    vec3 p = ro+rd*t;
    for (int i=0; i<num_level; i++) {
        outy *= p.x*p.x + p.y*p.y + p.z*p.z - float(i)*(level_max-level_min)/float(num_level) - level_min;
    }

    return outy;
}

float ray(vec3 ro, vec3 rd, float tmin, float tmax, float eps) {
    float d = 0.05;
    float t = tmin;
    int imax = int(ceil(tmax/d));
    int nmax = 10;
    int i=0;
    float prev_v = level_intersection(ro,rd,tmin), v;
    
    if (abs(prev_v)<=eps) return prev_v;
    
    for (; i<=imax; i++) {
        v = level_intersection(ro,rd,t);
        if (abs(v)<=eps) return t;
        
        if (sign(v)!=sign(prev_v)) break;
        
        float xo = v*(t-tmin)/(prev_v-v);
        
        if (i<nmax && xo>0.0 && xo<d) t+=xo;
        else t+=d;
        continue;
    }

    for (; i<=imax; i++) {
        float v_new = level_intersection(ro,rd,t);
        if (abs(v_new)<=eps) return t;
        
        float xo = v_new*(t-tmin)/(prev_v-v_new);
        
        if (sign(v_new)!=sign(prev_v)) {
            tmax = t;
            v=v_new;
        } else if (sign(v_new)!=sign(v)) {
            tmin = t;
            prev_v=v_new;
        }
        
        if (abs(xo)<(tmax-tmin)/2.0 && t+xo<tmax && t+xo>tmin) {
            t+=xo;
        } else {
            t=(tmax+tmin)/2.0;
        }
    }
    
    return t;
}

vec3 normal(vec2 pt) {
    const int tbsize = 3;
    float d = 0.1;
    const float scale = 2.0;
    
    float tb[tbsize+tbsize-1];

#define CRAP(l,r) tb[0] = (r-l)/(2.0*d); \
    for (int i=0; i<tbsize; i++) { \
        d/=scale; \
        tb[tbsize-1] = (r-l)/(2.0*d); \
        float rat = scale*scale; \
        \
        for (int j=1; j<=i; j++) { \
            tb[tbsize-1+j] = (tb[tbsize+j-2]*rat - tb[j-1])/(rat-1.0); \
        }\
        \
        for (int j=0; j<=i; j++) {\
            tb[j] = tb[j+tbsize-1];\
        }\
    }
    
    CRAP(func(pt.x+d,pt.y),func(pt.x-d,pt.y))
    float dyx = tb[tbsize-1];

    d = 0.1;
    CRAP(func(pt.x,pt.y+d),func(pt.x,pt.y-d))
    float dyz = tb[tbsize-1];
    
    return normalize(vec3(dyx,-1,dyz));
}

#define PI 3.1415926538

vec4 grid(vec2 pos) {
    const float scale = 10.0;
    float width = 0.02;
    const float maxd = 20.0;
    
    if (abs(pos.x)>maxd || abs(pos.y)>maxd) return vec4(0,0,0,0);

    pos /= scale;
    pos-=round(pos);
    
    
    if (abs(pos.x)<=width) {
        return vec4(1,1,0,1);
    } else if (abs(pos.y)<=width) {
        return vec4(1,0,0,1);
    }
    
    return vec4(0,0,0,0);
}

float axes_dist(vec2 pos, vec2 line) {
    float len = 0.1*length(line);
    float dpl = dot(pos,line);
    float proj = dpl*dpl/dot(line,line);
    if (dpl<0.0 || proj>len*len) return 1.0/0.0;
    return sqrt(dot(pos,pos) - proj);
}

vec4 axes(vec2 center, vec2 pos, float axz, float av) {
    pos -= center;
    
    const float width = 0.01;
    
    vec2 to_x = vec2(sin(axz),-cos(axz)*sin(av));
    if (axes_dist(pos,to_x)<=width) return vec4(1,0,0,1);
    vec2 to_y = vec2(cos(axz),sin(axz)*sin(av));
    if (axes_dist(pos,to_y)<=width) return vec4(1,1,0,1);
    vec2 to_z = vec2(0.0,cos(av));
    if (axes_dist(pos,to_z)<=width) return vec4(0,0,1,1);
    
    return vec4(0,0,0,0);
}

const float dielectric_baserefl = 0.04;

float do_fresnel(float is_metal, float view_offset) {
  float baserefl = mix(dielectric_baserefl, 0.5, is_metal);
  float fresnel = baserefl + (1.0-baserefl)*pow(1.0-view_offset, 5.0);

  return fresnel;
}

vec4 light(vec4 color, vec4 light_color, vec3 normal, vec3 light_angle, vec3 view_angle) {
    float is_metal = 0.5;
  float view_normal_angle = (1.0+dot(view_angle, normal))/2.0;
    float fresnel = do_fresnel(is_metal, view_normal_angle);
  vec3 half_angle = normalize(light_angle + view_angle); //microsurface normal
  
  float light_normal_angle = dot(light_angle, normal);

  float view_half_angle = dot(view_angle, half_angle);
  float normal_half_angle = dot(half_angle, normal);

  if (light_normal_angle<0.0) return vec4(0);

  float roughsq = 0.2*0.2;

  //offset exponents from tangent/"height" (2, 0.5) -> (1, -1)
  //lmao
  float view_shadow = view_normal_angle/(view_normal_angle*(1.0-roughsq) + roughsq);
  float light_shadow = light_normal_angle/(light_normal_angle*(1.0-roughsq) + roughsq);

  //equivalent to dividing roughness/spread by ellipse where x is scaled by roughness squared
  float ggx = roughsq/(1.0 - normal_half_angle*normal_half_angle*(1.0-roughsq));

    float spec = fresnel*ggx;

  return color*view_shadow*light_shadow*normal_half_angle*light_color.a
    * ((1.0-is_metal) + is_metal*light_color*spec);
}

const float opacity = 0.3;

void main() {
    float anglexz = fract(mouse.x/2)*2*PI;
    float anglev = (fract(mouse.y) - 0.5)*PI;
    
    float r = 30.0;
    
    float power = 0.4;
    
//    fragtexpos /= iResolution.y;
//    fragtexpos *= 2.0;
    vec2 fragcoord = 2.0*fragtexpos-1.0;

    float aspect = resolution.x/resolution.y;
    fragcoord.x *= aspect;
//    fragcoord -= vec2(aspect, 1.0);

    vec3 rd = -normalize(vec3(1.0,power*fragcoord.y,power*fragcoord.x));
    rd = mat3(vec3(cos(anglev),sin(anglev),0),vec3(-sin(anglev),cos(anglev),0),vec3(0,0,1))*rd;
    rd = mat3(vec3(cos(anglexz),0,sin(anglexz)),vec3(0,1,0),vec3(-sin(anglexz),0,cos(anglexz)))*rd;
    
    vec3 ro = r*vec3(cos(anglexz)*cos(anglev), sin(anglev), sin(anglexz)*cos(anglev));
    
    float tmax = 100.0;
    float t = ray(ro,rd,0.001,tmax,0.0001);

    if (sign(ro.y*rd.y)==-1.0) {
        float gt = -ro.y/rd.y;
        if (gt<t || t>tmax) {
            vec3 grid_pos = ro+rd*gt;
            vec4 gridcol = grid(vec2(grid_pos.x, grid_pos.z));
            if (gridcol.w!=0.0) {
                outColor = gridcol;
                return;
            }
        }
    }
    
    vec2 axes_center = vec2(-aspect+0.1, -0.9);
    outColor += axes(axes_center, fragcoord, anglexz, -anglev);
    if (t>tmax) return;

    int i=0;
    for (; i<num_cast && t<=tmax; i++) {
        vec3 pos = ro+rd*t;
        vec3 n = normal(vec2(pos.x,pos.z));

        vec3 lightd = normalize(vec3(-1,1,-1));
        vec3 lightpt = vec3(0,-10,10);

        vec3 va = normalize(ro-pos);
//        vec4 contrib = vec4(0.1);
//        contrib += light(vec4(1,1,1,1),vec4(1,1,1,1),n,lightd,va);
//        contrib += light(vec4(1,1,1,1),vec4(1,1,1,1),n,normalize(lightpt-pos),va);
        float spec = 0.0;
        float ptlen = length(lightpt-pos);
        spec += max(0.0,dot(n,lightpt-pos)/(ptlen*ptlen));
        spec += max(0.0, dot(n,-lightd));
        outColor += vec4(spec*opacity);

        if (i!=num_cast-1) {
            t += ray(pos,rd,0.01,tmax,0.00001);
        }
    }

    outColor = vec4(pow(vec3(outColor), vec3(1.0/2.2)), 1.0);

//    outColor = vec4(float(i)/float(num_cast));
//    outColor = vec4(1.0)*spec;
})"