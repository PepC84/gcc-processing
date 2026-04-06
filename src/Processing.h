#pragma once

// Include platform shims first (provides termios/glob stubs on Windows)
#if __has_include("Platform.h")
#  include "Platform.h"
#endif

// Windows (MSVC/MinGW) doesn't define M_PI unless this is set before <cmath>
#ifndef _USE_MATH_DEFINES
#  define _USE_MATH_DEFINES
#endif
#include <cmath>
// Fallback in case _USE_MATH_DEFINES wasn't enough (some MinGW configs)
#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

#include <iostream>
#include <sstream>
#include <fstream>
#include <regex>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <string>
#include <vector>
#include <thread>
#include <functional>
#include <algorithm>
#include <memory>
#include <map>
#include <iomanip>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

namespace Processing {

// =============================================================================
// PVECTOR
// =============================================================================

class PVector {
public:
    float x,y,z;
    PVector():x(0),y(0),z(0){}
    PVector(float x,float y):x(x),y(y),z(0){}
    PVector(float x,float y,float z):x(x),y(y),z(z){}
    PVector& set(float _x,float _y,float _z=0){x=_x;y=_y;z=_z;return*this;}
    PVector& set(const PVector& v){x=v.x;y=v.y;z=v.z;return*this;}
    PVector  copy()const{return PVector(x,y,z);}
    float mag()const{return std::sqrt(x*x+y*y+z*z);}
    float magSq()const{return x*x+y*y+z*z;}
    PVector& add(float _x,float _y,float _z=0){x+=_x;y+=_y;z+=_z;return*this;}
    PVector& add(const PVector& v){x+=v.x;y+=v.y;z+=v.z;return*this;}
    PVector& sub(float _x,float _y,float _z=0){x-=_x;y-=_y;z-=_z;return*this;}
    PVector& sub(const PVector& v){x-=v.x;y-=v.y;z-=v.z;return*this;}
    PVector& mult(float s){x*=s;y*=s;z*=s;return*this;}
    PVector& div(float s){x/=s;y/=s;z/=s;return*this;}
    static PVector add(const PVector& a,const PVector& b){return PVector(a.x+b.x,a.y+b.y,a.z+b.z);}
    static PVector sub(const PVector& a,const PVector& b){return PVector(a.x-b.x,a.y-b.y,a.z-b.z);}
    static PVector mult(const PVector& v,float s){return PVector(v.x*s,v.y*s,v.z*s);}
    static PVector div(const PVector& v,float s){return PVector(v.x/s,v.y/s,v.z/s);}
    PVector operator+(const PVector& v)const{return PVector(x+v.x,y+v.y,z+v.z);}
    PVector operator-(const PVector& v)const{return PVector(x-v.x,y-v.y,z-v.z);}
    PVector operator*(float s)const{return PVector(x*s,y*s,z*s);}
    PVector operator/(float s)const{return PVector(x/s,y/s,z/s);}
    PVector& operator+=(const PVector& v){return add(v);}
    PVector& operator-=(const PVector& v){return sub(v);}
    PVector& operator*=(float s){return mult(s);}
    PVector& operator/=(float s){return div(s);}
    bool operator==(const PVector& v)const{return x==v.x&&y==v.y&&z==v.z;}
    bool operator!=(const PVector& v)const{return!(*this==v);}
    float dot(const PVector& v)const{return x*v.x+y*v.y+z*v.z;}
    float dot(float _x,float _y,float _z=0)const{return x*_x+y*_y+z*_z;}
    static float dot(const PVector& a,const PVector& b){return a.dot(b);}
    PVector cross(const PVector& v)const{return PVector(y*v.z-z*v.y,z*v.x-x*v.z,x*v.y-y*v.x);}
    static PVector cross(const PVector& a,const PVector& b){return a.cross(b);}
    PVector& normalize(){float m=mag();if(m>0)div(m);return*this;}
    PVector  normalized()const{PVector v(*this);return v.normalize();}
    PVector& limit(float mx){if(magSq()>mx*mx){normalize();mult(mx);}return*this;}
    PVector& setMag(float m){normalize();mult(m);return*this;}
    float dist(const PVector& v)const{float dx=x-v.x,dy=y-v.y,dz=z-v.z;return std::sqrt(dx*dx+dy*dy+dz*dz);}
    static float dist(const PVector& a,const PVector& b){return a.dist(b);}
    float heading()const{return std::atan2(y,x);}
    float angleBetween(const PVector& v)const{float m=mag()*v.mag();if(m==0)return 0;float c=dot(v)/m;c=c<-1?-1:c>1?1:c;return std::acos(c);}
    static float angleBetween(const PVector& a,const PVector& b){return a.angleBetween(b);}
    PVector& rotate(float t){float c=std::cos(t),s=std::sin(t),nx=x*c-y*s,ny=x*s+y*c;x=nx;y=ny;return*this;}
    PVector& lerp(const PVector& v,float t){x+=(v.x-x)*t;y+=(v.y-y)*t;z+=(v.z-z)*t;return*this;}
    PVector& lerp(float _x,float _y,float _z,float t){x+=(_x-x)*t;y+=(_y-y)*t;z+=(_z-z)*t;return*this;}
    static PVector lerp(const PVector& a,const PVector& b,float t){return PVector(a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t);}
    static PVector fromAngle(float a,float len=1.0f){return PVector(std::cos(a)*len,std::sin(a)*len);}
    static PVector random2D(){float a=static_cast<float>(rand())/RAND_MAX*6.28318f;return fromAngle(a);}
    static PVector random3D(){float t=static_cast<float>(rand())/RAND_MAX*6.28318f,p=std::acos(2.0f*static_cast<float>(rand())/RAND_MAX-1.0f);return PVector(std::sin(p)*std::cos(t),std::sin(p)*std::sin(t),std::cos(p));}
    std::string toString()const{std::ostringstream ss;ss<<"[ "<<x<<", "<<y<<", "<<z<<" ]";return ss.str();}
};

// =============================================================================
// PCOLOR
// =============================================================================

class PColor {
public:
    float r,g,b,a;
    PColor():r(0),g(0),b(0),a(255){}
    PColor(float gray):r(gray),g(gray),b(gray),a(255){}
    PColor(float gray,float a):r(gray),g(gray),b(gray),a(a){}
    PColor(float r,float g,float b):r(r),g(g),b(b),a(255){}
    PColor(float r,float g,float b,float a):r(r),g(g),b(b),a(a){}
    explicit PColor(unsigned int argb):r((argb>>16)&0xFF),g((argb>>8)&0xFF),b(argb&0xFF),a((argb>>24)&0xFF){}
    unsigned int toARGB()const{int ri=(int)std::fmax(0,std::fmin(255,r)),gi=(int)std::fmax(0,std::fmin(255,g)),bi=(int)std::fmax(0,std::fmin(255,b)),ai=(int)std::fmax(0,std::fmin(255,a));return(ai<<24)|(ri<<16)|(gi<<8)|bi;}
    float rf()const{return r/255.0f;}float gf()const{return g/255.0f;}float bf()const{return b/255.0f;}float af()const{return a/255.0f;}
    PColor& set(float _r,float _g,float _b,float _a=255){r=_r;g=_g;b=_b;a=_a;return*this;}
    PColor& set(float gray,float _a=255){r=g=b=gray;a=_a;return*this;}
    PColor copy()const{return PColor(r,g,b,a);}
    float hue()const{float rf_=r/255.0f,gf_=g/255.0f,bf_=b/255.0f,mx=std::fmax(rf_,std::fmax(gf_,bf_)),mn=std::fmin(rf_,std::fmin(gf_,bf_)),d=mx-mn;if(d==0)return 0;float h=(mx==rf_)?(gf_-bf_)/d:(mx==gf_)?2.0f+(bf_-rf_)/d:4.0f+(rf_-gf_)/d;h*=60.0f;if(h<0)h+=360.0f;return h;}
    float saturation()const{float mx=std::fmax(r,std::fmax(g,b)),mn=std::fmin(r,std::fmin(g,b));return mx==0?0:((mx-mn)/mx)*100.0f;}
    float brightness()const{return std::fmax(r,std::fmax(g,b))/255.0f*100.0f;}
    static PColor fromHSB(float h,float s,float bv,float a=255){s/=100.0f;bv/=100.0f;if(s==0){float v=bv*255.0f;return PColor(v,v,v,a);}float hh=std::fmod(h,360.0f)/60.0f;int i=(int)hh;float f=hh-i,p=bv*(1-s),q=bv*(1-s*f),t=bv*(1-s*(1-f)),rv,gv,blv;switch(i){case 0:rv=bv;gv=t;blv=p;break;case 1:rv=q;gv=bv;blv=p;break;case 2:rv=p;gv=bv;blv=t;break;case 3:rv=p;gv=q;blv=bv;break;case 4:rv=t;gv=p;blv=bv;break;default:rv=bv;gv=p;blv=q;break;}return PColor(rv*255,gv*255,blv*255,a);}
    PColor operator+(const PColor& o)const{return PColor(r+o.r,g+o.g,b+o.b,a+o.a);}
    PColor operator-(const PColor& o)const{return PColor(r-o.r,g-o.g,b-o.b,a-o.a);}
    PColor operator*(float s)const{return PColor(r*s,g*s,b*s,a*s);}
    PColor operator/(float s)const{return PColor(r/s,g/s,b/s,a/s);}
    PColor& operator+=(const PColor& o){r+=o.r;g+=o.g;b+=o.b;a+=o.a;return*this;}
    PColor& operator-=(const PColor& o){r-=o.r;g-=o.g;b-=o.b;a-=o.a;return*this;}
    PColor& operator*=(float s){r*=s;g*=s;b*=s;a*=s;return*this;}
    PColor& operator/=(float s){r/=s;g/=s;b/=s;a/=s;return*this;}
    bool operator==(const PColor& o)const{return r==o.r&&g==o.g&&b==o.b&&a==o.a;}
    bool operator!=(const PColor& o)const{return!(*this==o);}
    static PColor lerp(const PColor& c1,const PColor& c2,float t){return PColor(c1.r+(c2.r-c1.r)*t,c1.g+(c2.g-c1.g)*t,c1.b+(c2.b-c1.b)*t,c1.a+(c2.a-c1.a)*t);}
    PColor& clamp(){r=std::fmax(0,std::fmin(255,r));g=std::fmax(0,std::fmin(255,g));b=std::fmax(0,std::fmin(255,b));a=std::fmax(0,std::fmin(255,a));return*this;}
    PColor multRGB(float s)const{return PColor(r*s,g*s,b*s,a);}
    static PColor blend(const PColor& src,const PColor& dst){float sa=src.a/255.0f;return PColor(src.r*sa+dst.r*(1-sa),src.g*sa+dst.g*(1-sa),src.b*sa+dst.b*(1-sa),255);}
    static PColor add(const PColor& a,const PColor& b){return PColor(std::fmin(255,a.r+b.r),std::fmin(255,a.g+b.g),std::fmin(255,a.b+b.b),a.a);}
    static PColor multiply(const PColor& a,const PColor& b){return PColor((a.r/255.0f)*b.r,(a.g/255.0f)*b.g,(a.b/255.0f)*b.b,a.a);}
    static PColor screen(const PColor& a,const PColor& b){auto sc=[](float x,float y){return 255-(255-x)*(255-y)/255.0f;};return PColor(sc(a.r,b.r),sc(a.g,b.g),sc(a.b,b.b),a.a);}
    float brightness255()const{return std::fmax(r,std::fmax(g,b));}
    std::string toString()const{std::ostringstream ss;ss<<"PColor("<<r<<", "<<g<<", "<<b<<", "<<a<<")";return ss.str();}
};

void fill(const PColor& c);
void stroke(const PColor& c);
void background(const PColor& c);
void tint(const PColor& c);

// =============================================================================
// PIMAGE
// =============================================================================

static constexpr int GRAY        = 1;
static constexpr int INVERT      = 2;
static constexpr int THRESHOLD   = 3;
static constexpr int BLUR_FILTER = 4;
static constexpr int POSTERIZE   = 5;

class PImage {
public:
    int width=0,height=0;
    std::vector<unsigned int> pixels;
    GLuint texID=0;
    bool   dirty=false;
    PImage()=default;
    PImage(int w,int h):width(w),height(h),pixels(w*h,0xFF000000){}
    unsigned int get(int x,int y)const{if(x<0||x>=width||y<0||y>=height)return 0;return pixels[y*width+x];}
    void set(int x,int y,unsigned int c){if(x<0||x>=width||y<0||y>=height)return;pixels[y*width+x]=c;dirty=true;}
    void loadPixels(){}
    void updatePixels(){dirty=true;}
    void uploadTexture(){std::vector<unsigned char> rgba(width*height*4);for(int i=0;i<width*height;i++){unsigned int p=pixels[i];rgba[i*4+0]=(p>>16)&0xFF;rgba[i*4+1]=(p>>8)&0xFF;rgba[i*4+2]=p&0xFF;rgba[i*4+3]=(p>>24)&0xFF;}if(texID==0)glGenTextures(1,&texID);glBindTexture(GL_TEXTURE_2D,texID);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());glBindTexture(GL_TEXTURE_2D,0);dirty=false;}
    void resize(int w,int h){width=w;height=h;pixels.assign(w*h,0xFF000000);dirty=true;}
    void filter(int mode){for(auto& p:pixels){int r=(p>>16)&0xFF,g=(p>>8)&0xFF,b=p&0xFF,a=(p>>24)&0xFF;if(mode==GRAY){int gr=(r+g+b)/3;p=(a<<24)|(gr<<16)|(gr<<8)|gr;}else if(mode==INVERT){p=(a<<24)|((255-r)<<16)|((255-g)<<8)|(255-b);}else if(mode==THRESHOLD){int gr=(r+g+b)/3;int t=gr>127?255:0;p=(a<<24)|(t<<16)|(t<<8)|t;}}dirty=true;}
    PImage get(int x,int y,int w,int h)const{PImage out(w,h);for(int iy=0;iy<h;iy++)for(int ix=0;ix<w;ix++)out.pixels[iy*w+ix]=get(x+ix,y+iy);return out;}
    void copy(const PImage& src,int sx,int sy,int sw,int sh,int dx,int dy,int dw,int dh){for(int iy=0;iy<dh;iy++)for(int ix=0;ix<dw;ix++){int srcX=sx+(int)(ix*(float)sw/dw),srcY=sy+(int)(iy*(float)sh/dh);set(dx+ix,dy+iy,src.get(srcX,srcY));}dirty=true;}
    void mask(const PImage& m){for(int i=0;i<width*height&&i<(int)m.pixels.size();i++){int a=(m.pixels[i]>>16)&0xFF;pixels[i]=(pixels[i]&0x00FFFFFF)|(a<<24);}dirty=true;}
    ~PImage(){if(texID)glDeleteTextures(1,&texID);}
    PImage(const PImage&)=delete;
    PImage& operator=(const PImage&)=delete;
    PImage(PImage&& o)noexcept:width(o.width),height(o.height),pixels(std::move(o.pixels)),texID(o.texID),dirty(o.dirty){o.texID=0;}
};

// =============================================================================
// PGRAPHICS
// =============================================================================

class PGraphics:public PImage {
public:
    GLuint fbo=0,rbo=0;
    bool active=false;
    PGraphics()=default;
    PGraphics(int w,int h):PImage(w,h){glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);if(texID==0)glGenTextures(1,&texID);glBindTexture(GL_TEXTURE_2D,texID);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texID,0);glGenRenderbuffers(1,&rbo);glBindRenderbuffer(GL_RENDERBUFFER,rbo);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,w,h);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,rbo);glBindFramebuffer(GL_FRAMEBUFFER,0);}
    void beginDraw(){glBindFramebuffer(GL_FRAMEBUFFER,fbo);active=true;}
    void endDraw(){glBindFramebuffer(GL_FRAMEBUFFER,0);active=false;}
    ~PGraphics(){if(fbo)glDeleteFramebuffers(1,&fbo);if(rbo)glDeleteRenderbuffers(1,&rbo);}
    PGraphics(const PGraphics&)=delete;
    PGraphics& operator=(const PGraphics&)=delete;
};

// =============================================================================
// ENVIRONMENT STATE
// =============================================================================

extern int   winWidth,winHeight;
extern int   displayWidth,displayHeight;
extern int   pixelWidth,pixelHeight;
extern int   pixelDensityValue;
extern bool  isResizable,focused;
static inline int& width  = winWidth;
static inline int& height = winHeight;

// =============================================================================
// MOUSE
// =============================================================================

extern float mouseX,mouseY,pmouseX,pmouseY;
extern bool  isMousePressed;
extern int   mouseButton;

// Java-style event callbacks — define any of these in your sketch.
// On Linux/macOS: weak symbols let them be optional (sketch may omit them).
// On Windows (MinGW): weak symbols aren't supported, so we provide empty
// default implementations here; the sketch's definitions override them via
// the function pointer wiring in run().
#if defined(__GNUC__) && !defined(_WIN32)
// Linux / macOS — true weak symbols
void keyPressed()          __attribute__((weak));
void keyReleased()         __attribute__((weak));
void keyTyped()            __attribute__((weak));
void mousePressed()        __attribute__((weak));
void mouseReleased()       __attribute__((weak));
void mouseClicked()        __attribute__((weak));
void mouseMoved()          __attribute__((weak));
void mouseDragged()        __attribute__((weak));
void mouseWheel(int delta) __attribute__((weak));
void windowMoved()         __attribute__((weak));
void windowResized()       __attribute__((weak));
#else
// Windows (MinGW) — provide empty default bodies; sketch overrides via pointers
inline void keyPressed()          {}
inline void keyReleased()         {}
inline void keyTyped()            {}
inline void mousePressed()        {}
inline void mouseReleased()       {}
inline void mouseClicked()        {}
inline void mouseMoved()          {}
inline void mouseDragged()        {}
inline void mouseWheel(int /*d*/) {}
inline void windowMoved()         {}
inline void windowResized()       {}
#endif

// =============================================================================
// KEYBOARD
// =============================================================================

extern bool isKeyPressed;
extern int  keyCode;
extern char key;

// =============================================================================
// FRAME / LOOP
// =============================================================================

extern int   frameCount;
extern float currentFrameRate;
extern bool  looping;
extern float measuredFrameRate;

// =============================================================================
// DRAWING STATE
// =============================================================================

extern float fillR,fillG,fillB,fillA;
extern float strokeR,strokeG,strokeB,strokeA;
extern float strokeW;
extern bool  doFill,doStroke,smoothing;
extern int   currentRectMode,currentEllipseMode,currentImageMode;
extern float tintR,tintG,tintB,tintA;
extern bool  doTint;
extern int   colorModeVal;
extern float colorMaxH,colorMaxS,colorMaxB,colorMaxA;
extern std::vector<unsigned int> pixels;

// =============================================================================
// CONSTANTS
// =============================================================================

static constexpr int LEFT   = 0;
static constexpr int RIGHT  = 1;
static constexpr int CENTER = 2;

static constexpr int UP        = GLFW_KEY_UP;
static constexpr int DOWN      = GLFW_KEY_DOWN;
static constexpr int LEFT_KEY  = GLFW_KEY_LEFT;
static constexpr int RIGHT_KEY = GLFW_KEY_RIGHT;
static constexpr int ALT       = GLFW_KEY_LEFT_ALT;
static constexpr int CONTROL   = GLFW_KEY_LEFT_CONTROL;
static constexpr int SHIFT     = GLFW_KEY_LEFT_SHIFT;
static constexpr int BACKSPACE = GLFW_KEY_BACKSPACE;
static constexpr int TAB       = GLFW_KEY_TAB;
static constexpr int ENTER     = GLFW_KEY_ENTER;
static constexpr int ESC       = GLFW_KEY_ESCAPE;
static constexpr int DELETE_KEY= GLFW_KEY_DELETE;
static constexpr int CODED     = 0xFF;

static constexpr int RGB = 0;
static constexpr int HSB = 1;

static constexpr int CORNER      = 0;
static constexpr int CORNERS     = 1;
static constexpr int RADIUS      = 2;
static constexpr int CENTER_MODE = 3;

static constexpr int ROUND   = 10;
static constexpr int SQUARE  = 11;
static constexpr int PROJECT = 12;
static constexpr int MITER   = 13;
static constexpr int BEVEL   = 14;

static constexpr int POINTS         = 0;
static constexpr int LINES          = 1;
static constexpr int TRIANGLES      = 2;
static constexpr int TRIANGLE_FAN   = 3;
static constexpr int TRIANGLE_STRIP = 4;
static constexpr int QUADS          = 5;
static constexpr int QUAD_STRIP     = 6;
static constexpr int CLOSE          = 7;

static constexpr int LEFT_ALIGN   = 20;
static constexpr int RIGHT_ALIGN  = 21;
static constexpr int TOP_ALIGN    = 22;
static constexpr int BOTTOM_ALIGN = 23;
static constexpr int BASELINE     = 24;
static constexpr int CENTER_ALIGN = 25;  // text horizontal center

static constexpr int BLEND      = 30;
static constexpr int ADD        = 31;
static constexpr int SUBTRACT   = 32;
static constexpr int MULTIPLY   = 33;
static constexpr int SCREEN     = 34;
static constexpr int DARKEST    = 35;
static constexpr int LIGHTEST   = 36;
static constexpr int DIFFERENCE = 37;
static constexpr int EXCLUSION  = 38;

static constexpr float PI         = static_cast<float>(M_PI);
static constexpr float TWO_PI     = static_cast<float>(M_PI*2.0);
static constexpr float HALF_PI    = static_cast<float>(M_PI/2.0);
static constexpr float QUARTER_PI = static_cast<float>(M_PI/4.0);
static constexpr float TAU        = TWO_PI;

static constexpr int P2D = 2;
static constexpr int P3D = 3;

static constexpr int IMAGE  = 100;
static constexpr int NORMAL = 101;
static constexpr int CLAMP  = 102;
static constexpr int REPEAT = 103;

static constexpr int ENABLE_DEPTH_TEST          =  1;
static constexpr int DISABLE_DEPTH_TEST         = -1;
static constexpr int ENABLE_DEPTH_SORT          =  2;
static constexpr int DISABLE_DEPTH_SORT         = -2;
static constexpr int ENABLE_OPENGL_ERRORS       =  3;
static constexpr int DISABLE_OPENGL_ERRORS      = -3;
static constexpr int ENABLE_STROKE_PERSPECTIVE  =  4;
static constexpr int DISABLE_STROKE_PERSPECTIVE = -4;
static constexpr int ENABLE_TEXTURE_MIPMAPS     =  5;
static constexpr int DISABLE_TEXTURE_MIPMAPS    = -5;

static constexpr int ARROW       = GLFW_ARROW_CURSOR;
static constexpr int CROSS       = GLFW_CROSSHAIR_CURSOR;
static constexpr int HAND        = GLFW_POINTING_HAND_CURSOR;
static constexpr int MOVE        = GLFW_RESIZE_ALL_CURSOR;
static constexpr int TEXT_CURSOR = GLFW_IBEAM_CURSOR;
static constexpr int WAIT        = GLFW_RESIZE_ALL_CURSOR;

// =============================================================================
// TIMING
// =============================================================================

inline unsigned long millis(){using namespace std::chrono;static auto start=steady_clock::now();return static_cast<unsigned long>(duration_cast<milliseconds>(steady_clock::now()-start).count());}
inline int second(){std::time_t t=std::time(nullptr);return std::localtime(&t)->tm_sec;}
inline int minute(){std::time_t t=std::time(nullptr);return std::localtime(&t)->tm_min;}
inline int hour()  {std::time_t t=std::time(nullptr);return std::localtime(&t)->tm_hour;}
inline int day()   {std::time_t t=std::time(nullptr);return std::localtime(&t)->tm_mday;}
inline int month() {std::time_t t=std::time(nullptr);return std::localtime(&t)->tm_mon+1;}
inline int year()  {std::time_t t=std::time(nullptr);return std::localtime(&t)->tm_year+1900;}

// =============================================================================
// MATH
// =============================================================================

inline float sin(float x)   {return std::sin(x);}
inline float cos(float x)   {return std::cos(x);}
inline float tan(float x)   {return std::tan(x);}
inline float asin(float x)  {return std::asin(x);}
inline float acos(float x)  {return std::acos(x);}
inline float atan(float x)  {return std::atan(x);}
inline float atan2(float y,float x){return std::atan2(y,x);}
inline float sqrt(float x)  {return std::sqrt(x);}
inline float sq(float x)    {return x*x;}
inline float abs(float x)   {return std::fabs(x);}
inline float ceil(float x)  {return std::ceil(x);}
inline float floor(float x) {return std::floor(x);}
inline float round(float x) {return std::round(x);}
inline float exp(float x)   {return std::exp(x);}
inline float log(float x)   {return std::log(x);}
inline float pow(float b,float e){return std::pow(b,e);}
inline float mag(float x,float y){return std::sqrt(x*x+y*y);}
inline float mag(float x,float y,float z){return std::sqrt(x*x+y*y+z*z);}
inline float norm(float v,float lo,float hi){return(v-lo)/(hi-lo);}
inline float degrees(float r){return r*180.0f/PI;}
inline float radians(float d){return d*PI/180.0f;}
inline float lerp(float a,float b,float t){return a+t*(b-a);}
inline float dist(float x1,float y1,float x2,float y2){float dx=x2-x1,dy=y2-y1;return std::sqrt(dx*dx+dy*dy);}
inline float dist(float x1,float y1,float z1,float x2,float y2,float z2){float dx=x2-x1,dy=y2-y1,dz=z2-z1;return std::sqrt(dx*dx+dy*dy+dz*dz);}
inline float map(float v,float i0,float i1,float o0,float o1){return o0+(v-i0)*(o1-o0)/(i1-i0);}
inline float constrain(float v,float lo,float hi){return v<lo?lo:(v>hi?hi:v);}
inline float max(float a,float b){return a>b?a:b;}
inline float min(float a,float b){return a<b?a:b;}
inline float max(float a,float b,float c){return max(a,max(b,c));}
inline float min(float a,float b,float c){return min(a,min(b,c));}
inline bool  isNaN(float v){return std::isnan(v);}
inline bool  isInfinite(float v){return std::isinf(v);}

// =============================================================================
// RANDOM / NOISE
// =============================================================================

inline void  randomSeed(int s){std::srand(static_cast<unsigned>(s));}
inline float random(float lo,float hi){return lo+static_cast<float>(rand())/static_cast<float>(RAND_MAX)*(hi-lo);}
inline float random(float hi){return random(0.0f,hi);}
float randomGaussian();
void  noiseSeed(int seed);
void  noiseDetail(int octaves,float falloff=0.5f);
float noise(float x);
float noise(float x,float y);
float noise(float x,float y,float z);

// =============================================================================
// ARRAY FUNCTIONS
// =============================================================================

template<typename T> inline std::vector<T> append(std::vector<T> arr,T val){arr.push_back(val);return arr;}
template<typename T> inline std::vector<T> concat(std::vector<T> a,const std::vector<T>& b){a.insert(a.end(),b.begin(),b.end());return a;}
template<typename T> inline std::vector<T> expand(std::vector<T> arr,int newSize=-1){if(newSize<0)newSize=arr.size()*2;arr.resize(newSize);return arr;}
template<typename T> inline std::vector<T> reverse(std::vector<T> arr){std::reverse(arr.begin(),arr.end());return arr;}
template<typename T> inline std::vector<T> shorten(std::vector<T> arr){if(!arr.empty())arr.pop_back();return arr;}
template<typename T> inline std::vector<T> sort(std::vector<T> arr){std::sort(arr.begin(),arr.end());return arr;}
template<typename T> inline std::vector<T> splice(std::vector<T> arr,T val,int index){arr.insert(arr.begin()+index,val);return arr;}
template<typename T> inline std::vector<T> splice(std::vector<T> arr,const std::vector<T>& vals,int index){arr.insert(arr.begin()+index,vals.begin(),vals.end());return arr;}
template<typename T> inline std::vector<T> subset(const std::vector<T>& arr,int start,int count=-1){if(count<0)count=arr.size()-start;return std::vector<T>(arr.begin()+start,arr.begin()+start+count);}
template<typename T> inline void arrayCopy(const std::vector<T>& src,std::vector<T>& dst){dst=src;}
template<typename T> inline void arrayCopy(const std::vector<T>& src,int srcPos,std::vector<T>& dst,int dstPos,int len){for(int i=0;i<len;i++)dst[dstPos+i]=src[srcPos+i];}

// =============================================================================
// COLOR TYPE
// =============================================================================

struct color {
    unsigned int value;
    color():value(0xFF000000){}
    color(unsigned int v):value(v){}
    // All constructors route through makeColor so colorMode is respected.
    // Declared here, defined in Processing.cpp.
    color(int gray);
    color(int gray,int a);
    color(int r,int g,int b);
    color(int r,int g,int b,int a);
    color(float gray);
    color(float gray,float a);
    color(float r,float g,float b);
    color(float r,float g,float b,float a);
    operator unsigned int()const{return value;}
    bool operator==(const color& o)const{return value==o.value;}
    bool operator!=(const color& o)const{return value!=o.value;}
};

color makeColor(float a,float b,float c,float d=255);
color makeColor(float gray,float alpha=255);
inline color colorVal(int r,int g,int b,int a=255){return color((unsigned int)(((a&0xFF)<<24)|((r&0xFF)<<16)|((g&0xFF)<<8)|(b&0xFF)));}

float red(color c);
float green(color c);
float blue(color c);
float alpha(color c);
float brightness(color c);
float saturation(color c);
float hue(color c);
color lerpColor(color c1,color c2,float t);

// =============================================================================
// OUTPUT / PRINT
// =============================================================================

template<typename T>inline void print(const T& v){std::cout<<v;std::cout.flush();}
template<typename T>inline void println(const T& v){std::cout<<v<<"\n";std::cout.flush();}
inline void println(){std::cout<<"\n";std::cout.flush();}
template<typename T>inline void printArray(const std::vector<T>& a){for(size_t i=0;i<a.size();i++)std::cout<<"["<<i<<"] "<<a[i]<<"\n";}
inline void printArray(const std::string* arr,int len){for(int i=0;i<len;i++)std::cout<<"["<<i<<"] "<<arr[i]<<"\n";}
inline void printArray(const int* arr,int len){for(int i=0;i<len;i++)std::cout<<"["<<i<<"] "<<arr[i]<<"\n";}
inline void printArray(const float* arr,int len){for(int i=0;i<len;i++)std::cout<<"["<<i<<"] "<<arr[i]<<"\n";}

// =============================================================================
// STRING / CONVERSION
// =============================================================================

inline std::string str(int v)   {return std::to_string(v);}
inline std::string str(float v) {return std::to_string(v);}
inline std::string str(bool v)  {return v?"true":"false";}
inline std::string str(char v)  {return std::string(1,v);}
inline bool  toBoolean(const std::string& s){return s=="true"||s=="1"||s=="yes";}
inline int   toInt(const std::string& s)    {return std::stoi(s);}
inline float toFloat(const std::string& s)  {return std::stof(s);}
inline char  toChar(int v)                  {return static_cast<char>(v);}
inline std::string trim(const std::string& s){size_t a=s.find_first_not_of(" \t\n\r"),b=s.find_last_not_of(" \t\n\r");return a==std::string::npos?"":s.substr(a,b-a+1);}
inline std::vector<std::string> split(const std::string& s,char d){std::vector<std::string> o;std::stringstream ss(s);std::string t;while(std::getline(ss,t,d))o.push_back(t);return o;}
inline std::vector<std::string> splitTokens(const std::string& s,const std::string& delims){std::vector<std::string> o;std::string cur;for(char c:s){if(delims.find(c)!=std::string::npos){if(!cur.empty()){o.push_back(cur);cur.clear();}}else cur+=c;}if(!cur.empty())o.push_back(cur);return o;}
inline std::string join(const std::vector<std::string>& v,const std::string& sep){std::string o;for(size_t i=0;i<v.size();i++){if(i)o+=sep;o+=v[i];}return o;}
inline std::string nf(float v,int digits){std::ostringstream ss;ss.precision(digits);ss<<std::fixed<<v;return ss.str();}
inline std::string nf(int v,int minDigits){std::ostringstream ss;ss<<std::setw(minDigits)<<std::setfill('0')<<v;return ss.str();}
inline std::string nf(float v,int left,int right){std::ostringstream ss;ss<<std::fixed<<std::setprecision(right)<<v;std::string s=ss.str();size_t dot=s.find('.');size_t intLen=(dot==std::string::npos)?s.size():dot;while((int)intLen<left){s="0"+s;intLen++;}return s;}
inline std::string nfc(float v,int digits){std::ostringstream ss;ss.precision(digits);ss<<std::fixed<<v;std::string s=ss.str();int dot=s.find('.');if(dot==std::string::npos)dot=s.size();for(int i=dot-3;i>0;i-=3)s.insert(i,",");return s;}
inline std::string nfp(float v,int digits){return(v>=0?"+":"")+nf(v,digits);}
inline std::string nfs(float v,int digits){return(v>=0?" ":"")+nf(v,digits);}
inline std::string hex(int v){std::ostringstream ss;ss<<std::uppercase<<std::hex<<v;return ss.str();}
inline std::string hex(int v,int digits){std::ostringstream ss;ss<<std::uppercase<<std::hex<<std::setw(digits)<<std::setfill('0')<<v;return ss.str();}
inline std::string binary(int v){std::string s;for(int i=31;i>=0;i--)s+=((v>>i)&1)?'1':'0';return s;}
inline int unhex(const std::string& s){return std::stoi(s,nullptr,16);}
inline int unbinary(const std::string& s){return std::stoi(s,nullptr,2);}
inline std::vector<std::string> match(const std::string& s,const std::string& pattern){std::vector<std::string> out;std::smatch m;std::regex re(pattern);if(std::regex_search(s,m,re))for(auto& x:m)out.push_back(x.str());return out;}
inline std::vector<std::vector<std::string>> matchAll(const std::string& s,const std::string& pattern){std::vector<std::vector<std::string>> out;std::regex re(pattern);auto begin=std::sregex_iterator(s.begin(),s.end(),re),end=std::sregex_iterator();for(auto it=begin;it!=end;++it){std::vector<std::string> row;for(auto& x:*it)row.push_back(x.str());out.push_back(row);}return out;}

// =============================================================================
// FILE I/O
// =============================================================================

inline std::vector<std::string> loadStrings(const std::string& path){std::vector<std::string> lines;std::ifstream f(path);std::string l;while(std::getline(f,l))lines.push_back(l);return lines;}
inline bool saveStrings(const std::string& path,const std::vector<std::string>& lines){std::ofstream f(path);if(!f)return false;for(auto& l:lines)f<<l<<"\n";return true;}
inline std::vector<unsigned char> loadBytes(const std::string& path){std::ifstream f(path,std::ios::binary);return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());}
inline bool saveBytes(const std::string& path,const std::vector<unsigned char>& data){std::ofstream f(path,std::ios::binary);if(!f)return false;f.write(reinterpret_cast<const char*>(data.data()),data.size());return true;}

// =============================================================================
// USER CALLBACKS
// =============================================================================

void setup();
void draw();

extern std::function<void()>    _onKeyPressed;
extern std::function<void()>    _onKeyReleased;
extern std::function<void()>    _onKeyTyped;
extern std::function<void()>    _onMousePressed;
extern std::function<void()>    _onMouseReleased;
extern std::function<void()>    _onMouseClicked;
extern std::function<void()>    _onMouseMoved;
extern std::function<void()>    _onMouseDragged;
extern std::function<void(int)> _onMouseWheel;
extern std::function<void()>    _onWindowMoved;
extern std::function<void()>    _onWindowResized;

// =============================================================================
// ENVIRONMENT
// =============================================================================

void size(int w,int h);
void size(int w,int h,int renderer);
void fullScreen();
void frameRate(int fps);
void settings();
void noLoop();
void loop();
void redraw();
void exit_sketch();
void windowTitle(const std::string& t);
void windowMove(int x,int y);
void windowResize(int w,int h);
void windowResizable(bool r);
void windowRatio(int w,int h);
void pixelDensity(int d);
inline int displayDensity(){return pixelDensityValue;}
void smooth();
void noSmooth();
void hint(int which);
void cursor();
void cursor(int type);
void noCursor();
inline void setTitle(const std::string& t){windowTitle(t);}
inline void setLocation(int x,int y){windowMove(x,y);}
inline void setResizable(bool r){windowResizable(r);}
inline void exit(){exit_sketch();}

// =============================================================================
// STYLE STACK
// =============================================================================

void push();
void pop();
void pushStyle();
void popStyle();
void pushMatrix();
void popMatrix();

// =============================================================================
// COLOR MODE
// =============================================================================

// Only two overloads — no ambiguous 3-arg inline.
// colorMode(RGB)              → sets all maxes to 255
// colorMode(HSB, 360, 100, 100)    → pass 4 args explicitly
void colorMode(int mode, float mx=255.0f);
void colorMode(int mode, float mH, float mS, float mB, float mA=255.0f);

// =============================================================================
// BACKGROUND / CLEAR
// =============================================================================

void background(float gray, float a=255.0f);
void background(float r, float g, float b, float a=255.0f);
void background(color c);
// int overloads — all forward to float versions, no ambiguity
inline void background(int gray)               { background((float)gray); }
inline void background(int gray, int a)        { background((float)gray,(float)a); }
inline void background(int r, int g, int b)    { background((float)r,(float)g,(float)b); }
inline void background(int r, int g, int b, int a){ background((float)r,(float)g,(float)b,(float)a); }
void clear();

// =============================================================================
// FILL / STROKE
// =============================================================================

// Core float versions
void fill(float gray, float a=255.0f);
void fill(float r, float g, float b, float a=255.0f);
void fill(color c);
void fill(const PColor& c);
// int overloads
inline void fill(int gray)                  { fill((float)gray); }
inline void fill(int gray, int a)           { fill((float)gray,(float)a); }
inline void fill(int r, int g, int b)       { fill((float)r,(float)g,(float)b); }
inline void fill(int r, int g, int b, int a){ fill((float)r,(float)g,(float)b,(float)a); }
void noFill();

// Core float versions
void stroke(float gray, float a=255.0f);
void stroke(float r, float g, float b, float a=255.0f);
void stroke(color c);
void stroke(const PColor& c);
// int overloads
inline void stroke(int gray)                  { stroke((float)gray); }
inline void stroke(int gray, int a)           { stroke((float)gray,(float)a); }
inline void stroke(int r, int g, int b)       { stroke((float)r,(float)g,(float)b); }
inline void stroke(int r, int g, int b, int a){ stroke((float)r,(float)g,(float)b,(float)a); }
void noStroke();
void strokeWeight(float w);
inline void strokeWeight(int w){ strokeWeight((float)w); }
void strokeCap(int cap);
void strokeJoin(int join);

// =============================================================================
// SHAPE ATTRIBUTES
// =============================================================================

void rectMode(int mode);
void ellipseMode(int mode);

// =============================================================================
// 2D PRIMITIVES
// =============================================================================

void point(float x, float y);
void line(float x1, float y1, float x2, float y2);
void line(float x1, float y1, float z1, float x2, float y2, float z2);
void ellipse(float cx, float cy, float w, float h);
void circle(float cx, float cy, float d);
void rect(float x, float y, float w, float h);
void rect(float x, float y, float w, float h, float r);
void square(float x, float y, float s);
void triangle(float x1, float y1, float x2, float y2, float x3, float y3);
void quad(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4);
void arc(float cx, float cy, float w, float h, float start, float stop);

// int passthrough overloads so Java-style calls like rect(81,81,63,63) compile
template<typename A,typename B> inline void point(A x,B y){point((float)x,(float)y);}
template<typename A,typename B,typename C,typename D> inline void line(A x1,B y1,C x2,D y2){line((float)x1,(float)y1,(float)x2,(float)y2);}
// Template overload catches ALL mixed int/float/double combos automatically
template<typename A,typename B,typename C,typename D>
inline void ellipse(A cx,B cy,C w,D h){ellipse((float)cx,(float)cy,(float)w,(float)h);}
template<typename A,typename B,typename C> inline void circle(A cx,B cy,C d){circle((float)cx,(float)cy,(float)d);}
template<typename A,typename B,typename C,typename D> inline void rect(A x,B y,C w,D h){rect((float)x,(float)y,(float)w,(float)h);}
template<typename A,typename B,typename C> inline void square(A x,B y,C s){square((float)x,(float)y,(float)s);}
template<typename A,typename B,typename C,typename D,typename E,typename F> inline void triangle(A x1,B y1,C x2,D y2,E x3,F y3){triangle((float)x1,(float)y1,(float)x2,(float)y2,(float)x3,(float)y3);}
template<typename A,typename B,typename C,typename D,typename E,typename F,typename G,typename H> inline void quad(A x1,B y1,C x2,D y2,E x3,F y3,G x4,H y4){quad((float)x1,(float)y1,(float)x2,(float)y2,(float)x3,(float)y3,(float)x4,(float)y4);}

// =============================================================================
// 3D PRIMITIVES & TRANSFORMS
// =============================================================================

void box(float size);
void box(float w, float h, float d);
void sphere(float r);
void sphereDetail(int res);
void rotateX(float angle);
void rotateY(float angle);
void rotateZ(float angle);

// =============================================================================
// VERTEX / CUSTOM SHAPES
// =============================================================================

void beginShape(int kind=-1);
void endShape(int mode=0);
void vertex(float x, float y);
void vertex(float x, float y, float z);
void bezierVertex(float cx1, float cy1, float cx2, float cy2, float x, float y);
void quadraticVertex(float cx, float cy, float x, float y);
void curveVertex(float x, float y);
void beginContour();
void endContour();
void bezier(float x1, float y1, float cx1, float cy1, float cx2, float cy2, float x2, float y2);
void curve(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3);
float bezierPoint(float a, float b, float c, float d, float t);
float bezierTangent(float a, float b, float c, float d, float t);
float curvePoint(float a, float b, float c, float d, float t);
float curveTangent(float a, float b, float c, float d, float t);
void curveDetail(int d);
void curveTightness(float t);
void bezierDetail(int d);

// =============================================================================
// MATRIX TRANSFORM
// =============================================================================

void resetMatrix();
void applyMatrix(float n00,float n01,float n02,float n03,
                 float n10,float n11,float n12,float n13,
                 float n20,float n21,float n22,float n23,
                 float n30,float n31,float n32,float n33);
void translate(float x, float y);
void translate(float x, float y, float z);
void scale(float s);
void scale(float sx, float sy);
void rotate(float angle);
void shearX(float angle);
void shearY(float angle);
void printMatrix();
float screenX(float x, float y, float z=0);
float screenY(float x, float y, float z=0);
float screenZ(float x, float y, float z=0);
float modelX(float x, float y, float z=0);
float modelY(float x, float y, float z=0);
float modelZ(float x, float y, float z=0);

// =============================================================================
// CAMERA & LIGHTS
// =============================================================================

void camera();
void camera(float eyeX, float eyeY, float eyeZ,
            float centerX, float centerY, float centerZ,
            float upX, float upY, float upZ);
void beginCamera();
void endCamera();
void perspective();
void perspective(float fov, float aspect, float zNear, float zFar);
void ortho();
void ortho(float left, float right, float bottom, float top, float near, float far);
void frustum(float left, float right, float bottom, float top, float near, float far);
void printCamera();
void printProjection();

void lights();
void noLights();
void ambientLight(float r, float g, float b);
void ambientLight(float r, float g, float b, float x, float y, float z);
void directionalLight(float r, float g, float b, float nx, float ny, float nz);
void pointLight(float r, float g, float b, float x, float y, float z);
void spotLight(float r, float g, float b,
               float x, float y, float z,
               float nx, float ny, float nz,
               float angle, float concentration);
void lightFalloff(float constant, float linear, float quadratic);
void lightSpecular(float r, float g, float b);
void normal(float nx, float ny, float nz);

void ambient(float r, float g, float b);
void ambient(color c);
void emissive(float r, float g, float b);
void emissive(color c);
void specular(float r, float g, float b);
void specular(color c);
void shininess(float s);

// =============================================================================
// TEXT
// =============================================================================

void text(const std::string& msg, float x, float y);
void text(int val, float x, float y);
void text(float val, float x, float y);
void textSize(float size);
void textAlign(int alignX, int alignY=-1);
void textLeading(float leading);
void textMode(int mode);
float textWidth(const std::string& s);
float textAscent();
float textDescent();

// =============================================================================
// IMAGE
// =============================================================================

PImage*    loadImage(const std::string& path);
PImage     createImage(int w, int h);
PGraphics* createGraphics(int w, int h);
void       image(PImage& img, float x, float y);
void       image(PImage& img, float x, float y, float w, float h);
void       imageMode(int mode);
void       tint(float gray, float a=255.0f);
void       tint(float r, float g, float b, float a=255.0f);
void       noTint();
void       filter(int mode);
void       filter(int mode, float param);
void       loadPixels();
void       updatePixels();
color      get(int x, int y);
void       set(int x, int y, color c);
PImage     getRegion(int x, int y, int w, int h);

// =============================================================================
// BLEND / CLIP
// =============================================================================

void blendMode(int mode);
void clip(float x, float y, float w, float h);
void noClip();
void blend(int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh, int mode);
void copy(int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh);

// =============================================================================
// SAVE / THREAD / UTILITY
// =============================================================================

void saveFrame(const std::string& filename="frame-####.png");
void save(const std::string& filename);
inline void thread(std::function<void()> fn){std::thread(fn).detach();}
inline void delay(int ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));}

// =============================================================================
// RUN
// =============================================================================

void run();

// =============================================================================
// JSON
// =============================================================================

struct JSONValue;
using JSONObject = std::map<std::string,JSONValue>;
using JSONArray  = std::vector<JSONValue>;

struct JSONValue {
    enum Type{NULL_T,BOOL_T,INT_T,FLOAT_T,STRING_T,ARRAY_T,OBJECT_T}type=NULL_T;
    bool b=false;double n=0;std::string s;
    std::shared_ptr<JSONArray> arr;std::shared_ptr<JSONObject> obj;
    JSONValue()=default;
    JSONValue(bool v):type(BOOL_T),b(v){}
    JSONValue(int v):type(INT_T),n(v){}
    JSONValue(double v):type(FLOAT_T),n(v){}
    JSONValue(const std::string& v):type(STRING_T),s(v){}
    JSONValue(const char* v):type(STRING_T),s(v){}
    JSONValue(JSONArray v):type(ARRAY_T),arr(std::make_shared<JSONArray>(v)){}
    JSONValue(JSONObject v):type(OBJECT_T),obj(std::make_shared<JSONObject>(v)){}
    bool isNull()const{return type==NULL_T;}bool isBool()const{return type==BOOL_T;}
    bool isInt()const{return type==INT_T;}bool isFloat()const{return type==FLOAT_T||type==INT_T;}
    bool isString()const{return type==STRING_T;}bool isArray()const{return type==ARRAY_T;}bool isObject()const{return type==OBJECT_T;}
    bool getBool()const{return b;}int getInt()const{return(int)n;}float getFloat()const{return(float)n;}std::string getString()const{return s;}
    JSONArray& getArray(){return*arr;}JSONObject& getObject(){return*obj;}
    const JSONArray& getArray()const{return*arr;}const JSONObject& getObject()const{return*obj;}
    JSONValue& operator[](const std::string& k){return(*obj)[k];}JSONValue& operator[](int i){return(*arr)[i];}
    int size()const{if(isArray())return arr->size();if(isObject())return obj->size();return 0;}
    bool hasKey(const std::string& k)const{return isObject()&&obj->count(k);}
};

JSONValue   parseJSON(const std::string& src);
std::string toJSONString(const JSONValue& v,int indent=0);
JSONValue   loadJSONObject(const std::string& path);
JSONValue   loadJSONArray(const std::string& path);
bool        saveJSONObject(const std::string& path,const JSONValue& v,int indent=2);
bool        saveJSONArray(const std::string& path,const JSONValue& v,int indent=2);
inline JSONValue parseJSONObject(const std::string& s){return parseJSON(s);}
inline JSONValue parseJSONArray(const std::string& s){return parseJSON(s);}

// =============================================================================
// XML
// =============================================================================

struct XML {
    std::string name,content;
    std::map<std::string,std::string> attributes;
    std::vector<XML> children;
    XML()=default;explicit XML(const std::string& n):name(n){}
    std::string getName()const{return name;}std::string getContent()const{return content;}
    bool hasAttribute(const std::string& k)const{return attributes.count(k)>0;}
    std::string getAttribute(const std::string& k,const std::string& def="")const{auto it=attributes.find(k);return it!=attributes.end()?it->second:def;}
    int getAttributeInt(const std::string& k,int def=0)const{return hasAttribute(k)?std::stoi(attributes.at(k)):def;}
    float getAttributeFloat(const std::string& k,float def=0)const{return hasAttribute(k)?std::stof(attributes.at(k)):def;}
    void setAttribute(const std::string& k,const std::string& v){attributes[k]=v;}
    void setContent(const std::string& c){content=c;}
    XML* addChild(const std::string& n){children.push_back(XML(n));return&children.back();}
    XML* getChild(int i){return i<(int)children.size()?&children[i]:nullptr;}
    XML* getChild(const std::string& n){for(auto& c:children)if(c.name==n)return&c;return nullptr;}
    int getChildCount()const{return children.size();}
    std::vector<XML*> getChildren(const std::string& n){std::vector<XML*> r;for(auto& c:children)if(c.name==n)r.push_back(&c);return r;}
    std::string toString(int indent=0)const;
};

XML  loadXML(const std::string& path);
XML  parseXML(const std::string& src);
bool saveXML(const std::string& path,const XML& x);

// =============================================================================
// TABLE
// =============================================================================

class Table {
public:
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
    Table()=default;
    void addColumn(const std::string& name){columns.push_back(name);}
    int getColumnCount()const{return columns.size();}int getRowCount()const{return rows.size();}
    std::string getColumnTitle(int i)const{return i<(int)columns.size()?columns[i]:"";}
    int getColumnIndex(const std::string& n)const{for(int i=0;i<(int)columns.size();i++)if(columns[i]==n)return i;return-1;}
    std::vector<std::string>& addRow(){rows.push_back(std::vector<std::string>(columns.size()));return rows.back();}
    std::string getString(int row,int col)const{return row<(int)rows.size()&&col<(int)rows[row].size()?rows[row][col]:"";}
    std::string getString(int row,const std::string& col)const{return getString(row,getColumnIndex(col));}
    int getInt(int row,int col)const{auto s=getString(row,col);return s.empty()?0:std::stoi(s);}
    int getInt(int row,const std::string& col)const{return getInt(row,getColumnIndex(col));}
    float getFloat(int row,int col)const{auto s=getString(row,col);return s.empty()?0:std::stof(s);}
    float getFloat(int row,const std::string& col)const{return getFloat(row,getColumnIndex(col));}
    void setString(int row,int col,const std::string& v){if(row<(int)rows.size()&&col<(int)rows[row].size())rows[row][col]=v;}
    void setString(int row,const std::string& col,const std::string& v){setString(row,getColumnIndex(col),v);}
    void setInt(int row,int col,int v){setString(row,col,std::to_string(v));}
    void setFloat(int row,int col,float v){setString(row,col,std::to_string(v));}
    std::vector<int> findRowsWithValue(const std::string& col,const std::string& val)const{std::vector<int> r;int c=getColumnIndex(col);for(int i=0;i<(int)rows.size();i++)if(getString(i,c)==val)r.push_back(i);return r;}
    int findFirstRowWithValue(const std::string& col,const std::string& val)const{auto r=findRowsWithValue(col,val);return r.empty()?-1:r[0];}
    void removeRow(int i){if(i<(int)rows.size())rows.erase(rows.begin()+i);}
    void clearRows(){rows.clear();}
};

Table* loadTable(const std::string& path,const std::string& options="header");
bool   saveTable(const std::string& path,const Table& t,const std::string& ext="csv");

// =============================================================================
// TYPED LIST / DICT
// =============================================================================

class IntList{public:std::vector<int>data;IntList()=default;IntList(std::initializer_list<int>l):data(l){}void append(int v){data.push_back(v);}void set(int i,int v){data[i]=v;}int get(int i)const{return data[i];}int size()const{return data.size();}void sort(){std::sort(data.begin(),data.end());}void reverse(){std::reverse(data.begin(),data.end());}bool hasValue(int v)const{return std::find(data.begin(),data.end(),v)!=data.end();}void remove(int i){data.erase(data.begin()+i);}void clear(){data.clear();}int&operator[](int i){return data[i];}};
class FloatList{public:std::vector<float>data;FloatList()=default;FloatList(std::initializer_list<float>l):data(l){}void append(float v){data.push_back(v);}void set(int i,float v){data[i]=v;}float get(int i)const{return data[i];}int size()const{return data.size();}void sort(){std::sort(data.begin(),data.end());}void reverse(){std::reverse(data.begin(),data.end());}void remove(int i){data.erase(data.begin()+i);}void clear(){data.clear();}float&operator[](int i){return data[i];}};
class StringList{public:std::vector<std::string>data;StringList()=default;StringList(std::initializer_list<std::string>l):data(l){}void append(const std::string&v){data.push_back(v);}void set(int i,const std::string&v){data[i]=v;}std::string get(int i)const{return data[i];}int size()const{return data.size();}void sort(){std::sort(data.begin(),data.end());}void reverse(){std::reverse(data.begin(),data.end());}bool hasValue(const std::string&v)const{return std::find(data.begin(),data.end(),v)!=data.end();}void remove(int i){data.erase(data.begin()+i);}void clear(){data.clear();}std::string&operator[](int i){return data[i];}};
class IntDict{public:std::map<std::string,int>data;void set(const std::string&k,int v){data[k]=v;}int get(const std::string&k,int def=0)const{auto it=data.find(k);return it!=data.end()?it->second:def;}bool hasKey(const std::string&k)const{return data.count(k)>0;}void remove(const std::string&k){data.erase(k);}int size()const{return data.size();}void clear(){data.clear();}std::vector<std::string>keys()const{std::vector<std::string>r;for(auto&p:data)r.push_back(p.first);return r;}int&operator[](const std::string&k){return data[k];}};
class FloatDict{public:std::map<std::string,float>data;void set(const std::string&k,float v){data[k]=v;}float get(const std::string&k,float def=0)const{auto it=data.find(k);return it!=data.end()?it->second:def;}bool hasKey(const std::string&k)const{return data.count(k)>0;}void remove(const std::string&k){data.erase(k);}int size()const{return data.size();}void clear(){data.clear();}std::vector<std::string>keys()const{std::vector<std::string>r;for(auto&p:data)r.push_back(p.first);return r;}float&operator[](const std::string&k){return data[k];}};
class StringDict{public:std::map<std::string,std::string>data;void set(const std::string&k,const std::string&v){data[k]=v;}std::string get(const std::string&k,const std::string&def="")const{auto it=data.find(k);return it!=data.end()?it->second:def;}bool hasKey(const std::string&k)const{return data.count(k)>0;}void remove(const std::string&k){data.erase(k);}int size()const{return data.size();}void clear(){data.clear();}std::vector<std::string>keys()const{std::vector<std::string>r;for(auto&p:data)r.push_back(p.first);return r;}std::string&operator[](const std::string&k){return data[k];}};

// =============================================================================
// PSHAPE
// =============================================================================

class PShape {
public:
    struct Vertex{float x,y,z,u,v;};
    std::vector<Vertex> verts;std::vector<PShape> children;
    int kind=-1;bool closed=false,visible=true;
    float fillR=1,fillG=1,fillB=1,fillA=1;
    float strokeR=0,strokeG=0,strokeB=0,strokeA=1,strokeW=1;
    bool hasFill=true,hasStroke=false;
    PShape()=default;explicit PShape(int k):kind(k){}
    void beginShape(int k=-1){kind=k;verts.clear();}void endShape(bool close=false){closed=close;}
    void vertex(float x,float y,float z=0,float u=0,float v=0){verts.push_back({x,y,z,u,v});}
    void addChild(const PShape&s){children.push_back(s);}
    PShape*getChild(int i){return i<(int)children.size()?&children[i]:nullptr;}
    int getChildCount()const{return children.size();}
    void setFill(float r,float g,float b,float a=1){fillR=r;fillG=g;fillB=b;fillA=a;hasFill=true;}
    void setStroke(float r,float g,float b,float a=1){strokeR=r;strokeG=g;strokeB=b;strokeA=a;hasStroke=true;}
    void setStrokeWeight(float w){strokeW=w;}void setVisible(bool v){visible=v;}
    int getVertexCount()const{return verts.size();}
    void translate(float x,float y,float z=0){for(auto&v:verts){v.x+=x;v.y+=y;v.z+=z;}}
    void scale(float s){for(auto&v:verts){v.x*=s;v.y*=s;v.z*=s;}}
    void scale(float sx,float sy){for(auto&v:verts){v.x*=sx;v.y*=sy;}}
};

PShape  createShape(int kind=-1);
PShape* loadShape(const std::string& path);
void    shape(const PShape& s,float x=0,float y=0);
void    shape(const PShape& s,float x,float y,float w,float h);
void    shapeMode(int mode);

// =============================================================================
// PFONT
// =============================================================================

struct PFont{
    std::string name;float size=12;bool loaded=false;
    PFont()=default;PFont(const std::string&n,float s):name(n),size(s),loaded(true){}
};
PFont loadFont(const std::string& filename);
PFont createFont(const std::string& name,float size,bool smooth=true);
void  textFont(const PFont& font);
void  textFont(const PFont& font,float size);

// =============================================================================
// TEXTURE
// =============================================================================

void textureMode(int mode);
void textureWrap(int mode);
void texture(PImage& img);

// =============================================================================
// FILE IO HELPERS
// =============================================================================

class BufferedReader{std::ifstream f;public:explicit BufferedReader(const std::string&path):f(path){}bool ready()const{return f.is_open()&&f.good();}std::string readLine(){std::string l;std::getline(f,l);return f?l:"";}void close(){f.close();}};
class PrintWriter{std::ofstream f;public:explicit PrintWriter(const std::string&path):f(path){}template<typename T>void print(const T&v){f<<v;}template<typename T>void println(const T&v){f<<v<<"\n";}void println(){f<<"\n";}void flush(){f.flush();}void close(){f.close();}};

BufferedReader* createReader(const std::string& path);
PrintWriter*    createWriter(const std::string& path);
inline bool saveStream(const std::string&path,const std::vector<unsigned char>&data){return saveBytes(path,data);}
inline void launch(const std::string&path){system(path.c_str());}
std::string selectInput(const std::string& prompt="",const std::string& filter="");
std::string selectOutput(const std::string& prompt="",const std::string& filter="");
std::string selectFolder(const std::string& prompt="");
PImage* requestImage(const std::string& path);
inline std::ifstream* createInput(const std::string&path){return new std::ifstream(path,std::ios::binary);}
inline std::ofstream* createOutput(const std::string&path){return new std::ofstream(path,std::ios::binary);}

// =============================================================================
// RECORD / RAW (stubs)
// =============================================================================

inline void beginRecord(const std::string&,const std::string&){}
inline void endRecord(){}
inline void beginRaw(const std::string&,const std::string&){}
inline void endRaw(){}

// =============================================================================
// PSHADER
// =============================================================================

class PShader{
public:
    GLuint program=0,vert=0,frag=0;std::string vertSrc,fragSrc;bool linked=false;
    PShader()=default;PShader(const std::string&v,const std::string&f):vertSrc(v),fragSrc(f){}
    static GLuint compileShader(GLenum type,const std::string&src){GLuint s=glCreateShader(type);const char*c=src.c_str();glShaderSource(s,1,&c,nullptr);glCompileShader(s);GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[512];glGetShaderInfoLog(s,512,nullptr,log);std::cerr<<"Shader compile error: "<<log<<"\n";}return s;}
    void compile(){vert=compileShader(GL_VERTEX_SHADER,vertSrc);frag=compileShader(GL_FRAGMENT_SHADER,fragSrc);program=glCreateProgram();glAttachShader(program,vert);glAttachShader(program,frag);glLinkProgram(program);GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);if(!ok){char log[512];glGetProgramInfoLog(program,512,nullptr,log);std::cerr<<"Shader link error: "<<log<<"\n";}linked=ok;}
    void bind(){if(linked)glUseProgram(program);}void unbind(){glUseProgram(0);}
    void set(const std::string&name,float v){glUniform1f(glGetUniformLocation(program,name.c_str()),v);}
    void set(const std::string&name,int v){glUniform1i(glGetUniformLocation(program,name.c_str()),v);}
    void set(const std::string&name,float x,float y){glUniform2f(glGetUniformLocation(program,name.c_str()),x,y);}
    void set(const std::string&name,float x,float y,float z){glUniform3f(glGetUniformLocation(program,name.c_str()),x,y,z);}
    void set(const std::string&name,float x,float y,float z,float w){glUniform4f(glGetUniformLocation(program,name.c_str()),x,y,z,w);}
    ~PShader(){if(program)glDeleteProgram(program);if(vert)glDeleteShader(vert);if(frag)glDeleteShader(frag);}
    PShader(const PShader&)=delete;PShader&operator=(const PShader&)=delete;
    PShader(PShader&&o)noexcept:program(o.program),vert(o.vert),frag(o.frag),vertSrc(o.vertSrc),fragSrc(o.fragSrc),linked(o.linked){o.program=o.vert=o.frag=0;}
};

PShader* loadShader(const std::string& fragPath,const std::string& vertPath="");
void     shader(PShader& s);
void     resetShader();

// =============================================================================
// GENERIC ARRAYLIST / HASHMAP
// =============================================================================

template<typename T>
class ArrayList{public:std::vector<T>data;ArrayList()=default;void add(const T&v){data.push_back(v);}void add(int i,const T&v){data.insert(data.begin()+i,v);}T&get(int i){return data[i];}const T&get(int i)const{return data[i];}void set(int i,const T&v){data[i]=v;}void remove(int i){data.erase(data.begin()+i);}bool remove(const T&v){auto it=std::find(data.begin(),data.end(),v);if(it==data.end())return false;data.erase(it);return true;}int size()const{return data.size();}bool isEmpty()const{return data.empty();}void clear(){data.clear();}bool contains(const T&v)const{return std::find(data.begin(),data.end(),v)!=data.end();}int indexOf(const T&v)const{auto it=std::find(data.begin(),data.end(),v);return it==data.end()?-1:(int)(it-data.begin());}void sort(){std::sort(data.begin(),data.end());}T&operator[](int i){return data[i];}const T&operator[](int i)const{return data[i];}typename std::vector<T>::iterator begin(){return data.begin();}typename std::vector<T>::iterator end(){return data.end();}};

template<typename K,typename V>
class HashMap{public:std::map<K,V>data;void put(const K&k,const V&v){data[k]=v;}V&get(const K&k){return data[k];}bool containsKey(const K&k)const{return data.count(k)>0;}bool containsValue(const V&v)const{for(auto&p:data)if(p.second==v)return true;return false;}void remove(const K&k){data.erase(k);}int size()const{return data.size();}bool isEmpty()const{return data.empty();}void clear(){data.clear();}std::vector<K>keySet()const{std::vector<K>r;for(auto&p:data)r.push_back(p.first);return r;}std::vector<V>values()const{std::vector<V>r;for(auto&p:data)r.push_back(p.second);return r;}V&operator[](const K&k){return data[k];}};

// =============================================================================
// TABLEROW
// =============================================================================

class TableRow{public:std::vector<std::string>*row=nullptr,*cols=nullptr;TableRow()=default;TableRow(std::vector<std::string>&r,std::vector<std::string>&c):row(&r),cols(&c){}std::string getString(int i)const{return(row&&i<(int)row->size())?(*row)[i]:"";}std::string getString(const std::string&col)const{if(!cols)return"";for(int i=0;i<(int)cols->size();i++)if((*cols)[i]==col)return getString(i);return"";}int getInt(int i)const{auto s=getString(i);return s.empty()?0:std::stoi(s);}int getInt(const std::string&c)const{auto s=getString(c);return s.empty()?0:std::stoi(s);}float getFloat(int i)const{auto s=getString(i);return s.empty()?0:std::stof(s);}float getFloat(const std::string&c)const{auto s=getString(c);return s.empty()?0:std::stof(s);}void setString(int i,const std::string&v){if(row&&i<(int)row->size())(*row)[i]=v;}void setInt(int i,int v){setString(i,std::to_string(v));}void setFloat(int i,float v){setString(i,std::to_string(v));}};

// =============================================================================
// PVECTOR HELPERS
// =============================================================================

inline PVector createVector(float x,float y,float z=0){return PVector(x,y,z);}

} // namespace Processing
