#include "Processing.h"
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <functional>
#include <memory>

// Uncomment + drop stb_image.h next to this file to enable loadImage():
// #define STB_IMAGE_IMPLEMENTATION
// #include "stb_image.h"

// Uncomment + drop stb_image_write.h to enable saveFrame()/save():
// #define STB_IMAGE_WRITE_IMPLEMENTATION
// #include "stb_image_write.h"

// stb_truetype — drop stb_truetype.h next to this file for TTF font rendering.
// default.ttf in the project root is loaded automatically as the default font.
#if __has_include("stb_truetype.h")
#  define STB_TRUETYPE_IMPLEMENTATION
#  include "stb_truetype.h"
#  define PROCESSING_HAS_STB_TRUETYPE 1
#else
#  define PROCESSING_HAS_STB_TRUETYPE 0
#endif

namespace Processing {

// =============================================================================
// STATE
// =============================================================================

int   winWidth=100,winHeight=100;
int   displayWidth=0,displayHeight=0;
int   pixelWidth=0,pixelHeight=0;
int   pixelDensityValue=1;
bool  isResizable=false,focused=false;

float mouseX=0,mouseY=0,pmouseX=0,pmouseY=0;
bool  isMousePressed=false;
int   mouseButton=-1;
bool isKeyPressed=false;
int   keyCode=0;
char  key=0;

int   frameCount=0;
float currentFrameRate=60.0f;
bool  looping=true;
static bool   redrawOnce=false;
float measuredFrameRate=0.0f;
static double targetFrameTime=1.0/60.0;

float fillR=1,fillG=1,fillB=1,fillA=1;
float strokeR=0,strokeG=0,strokeB=0,strokeA=1;
float strokeW=1;
bool  doFill=true,doStroke=true,smoothing=true;
int   currentRectMode=CORNER,currentEllipseMode=CENTER_MODE,currentImageMode=CORNER;
float tintR=1,tintG=1,tintB=1,tintA=1;
bool  doTint=false;
int   colorModeVal=RGB;
float colorMaxH=255.0f,colorMaxS=255.0f,colorMaxB=255.0f,colorMaxA=255.0f;

std::vector<unsigned int> pixels;

// User event callbacks
std::function<void()>    _onKeyPressed;
std::function<void()>    _onKeyReleased;
std::function<void()>    _onKeyTyped;
std::function<void()>    _onMousePressed;
std::function<void()>    _onMouseReleased;
std::function<void()>    _onMouseClicked;
std::function<void()>    _onMouseMoved;
std::function<void()>    _onMouseDragged;
std::function<void(int)> _onMouseWheel;
std::function<void()>    _onWindowMoved;
std::function<void()>    _onWindowResized;

static GLFWwindow* gWindow=nullptr;
static bool is3DMode=false;
static int   sphereRes=24;
static int   curveDetailVal=20;
static float curveTightnessVal=0.0f;
static int   bezierDetailVal=60;
static bool  lightsEnabled=false;
static int   lightIndex=0;

// Shape state
static int  shapeKind=-1;
static bool inShape=false,inContour=false;
static bool shape3D=false;
static std::vector<std::pair<float,float>> shapeVerts;
static std::vector<std::pair<float,float>> contourVerts;

// Style stack
struct Style {
    float fillR,fillG,fillB,fillA,strokeR,strokeG,strokeB,strokeA,strokeW;
    bool  doFill,doStroke;
    int   rectMode,ellipseMode,imageMode;
    float tintR,tintG,tintB,tintA;bool doTint;
    int   colorMode;float cmH,cmS,cmB,cmA;
};
static std::vector<Style> styleStack;

// =============================================================================
// NOISE (Perlin)
// =============================================================================

static int   noiseOctaves=4;
static float noiseFalloff=0.5f;
static int   noisePerm[512];

static void buildNoisePerm(int seed){
    srand(seed);
    for(int i=0;i<256;i++)noisePerm[i]=i;
    for(int i=255;i>0;i--){int j=rand()%(i+1);std::swap(noisePerm[i],noisePerm[j]);}
    for(int i=0;i<256;i++)noisePerm[256+i]=noisePerm[i];
}
void noiseSeed(int s){buildNoisePerm(s);}
void noiseDetail(int o,float f){noiseOctaves=o;noiseFalloff=f;}

static float fade(float t){return t*t*t*(t*(t*6-15)+10);}
static float nlerp(float a,float b,float t){return a+t*(b-a);}
static float grad(int h,float x,float y,float z){
    int hh=h&15;float u=hh<8?x:y,v=hh<4?y:hh==12||hh==14?x:z;
    return((hh&1)?-u:u)+((hh&2)?-v:v);
}
static float perlin3(float x,float y,float z){
    int X=((int)std::floor(x))&255,Y=((int)std::floor(y))&255,Z=((int)std::floor(z))&255;
    x-=std::floor(x);y-=std::floor(y);z-=std::floor(z);
    float u=fade(x),v=fade(y),w=fade(z);
    int A=noisePerm[X]+Y,AA=noisePerm[A]+Z,AB=noisePerm[A+1]+Z;
    int B=noisePerm[X+1]+Y,BA=noisePerm[B]+Z,BB=noisePerm[B+1]+Z;
    return nlerp(w,
        nlerp(v,nlerp(u,grad(noisePerm[AA],x,y,z),grad(noisePerm[BA],x-1,y,z)),
                nlerp(u,grad(noisePerm[AB],x,y-1,z),grad(noisePerm[BB],x-1,y-1,z))),
        nlerp(v,nlerp(u,grad(noisePerm[AA+1],x,y,z-1),grad(noisePerm[BA+1],x-1,y,z-1)),
                nlerp(u,grad(noisePerm[AB+1],x,y-1,z-1),grad(noisePerm[BB+1],x-1,y-1,z-1))));
}
static float noiseOctaved(float x,float y,float z){
    float result=0,amp=0.5f,freq=1,mx=0;
    for(int i=0;i<noiseOctaves;i++){result+=amp*(perlin3(x*freq,y*freq,z*freq)*0.5f+0.5f);mx+=amp;amp*=noiseFalloff;freq*=2;}
    return result/mx;
}
float noise(float x)             {return noiseOctaved(x,0,0);}
float noise(float x,float y)     {return noiseOctaved(x,y,0);}
float noise(float x,float y,float z){return noiseOctaved(x,y,z);}

float randomGaussian(){
    static float spare;static bool has=false;
    if(has){has=false;return spare;}
    float u,v,s;
    do{u=random(-1,1);v=random(-1,1);s=u*u+v*v;}while(s>=1);
    s=std::sqrt(-2*std::log(s)/s);spare=v*s;has=true;return u*s;
}

// =============================================================================
// COLOR MODE & HELPERS
// =============================================================================

static void hsbToRgb(float h,float s,float b,float& r,float& g,float& bl){
    h/=colorMaxH;s/=colorMaxS;b/=colorMaxB;
    if(s==0){r=g=bl=b;return;}
    float hh=h*6;int i=(int)hh;float f=hh-i;
    float p=b*(1-s),q=b*(1-s*f),t2=b*(1-s*(1-f));
    switch(i%6){
        case 0:r=b;g=t2;bl=p;break;case 1:r=q;g=b;bl=p;break;
        case 2:r=p;g=b;bl=t2;break;case 3:r=p;g=q;bl=b;break;
        case 4:r=t2;g=p;bl=b;break;default:r=b;g=p;bl=q;break;
    }
}

color makeColor(float a,float b,float c,float d){
    float r,g,bl,aa;
    if(colorModeVal==HSB){hsbToRgb(a,b,c,r,g,bl);aa=d/colorMaxA;}
    else{r=a/colorMaxH;g=b/colorMaxS;bl=c/colorMaxB;aa=d/colorMaxA;}
    return colorVal((int)(r*255),(int)(g*255),(int)(bl*255),(int)(aa*255));
}
color makeColor(float gray,float alpha){return makeColor(gray,gray,gray,alpha);}

// =============================================================================
// color STRUCT CONSTRUCTORS
// =============================================================================
color::color(int gray)              { value = makeColor((float)gray, colorMaxA).value; }
color::color(int gray, int a)       { value = makeColor((float)gray, (float)a).value; }
color::color(int r, int g, int b)   { value = makeColor((float)r,(float)g,(float)b,colorMaxA).value; }
color::color(int r,int g,int b,int a){ value = makeColor((float)r,(float)g,(float)b,(float)a).value; }
color::color(float gray)            { value = makeColor(gray, colorMaxA).value; }
color::color(float gray, float a)   { value = makeColor(gray, a).value; }
color::color(float r,float g,float b){ value = makeColor(r,g,b,colorMaxA).value; }
color::color(float r,float g,float b,float a){ value = makeColor(r,g,b,a).value; }

void colorMode(int mode,float mx){colorModeVal=mode;colorMaxH=colorMaxS=colorMaxB=colorMaxA=mx;}
void colorMode(int mode,float mH,float mS,float mB,float mA){colorModeVal=mode;colorMaxH=mH;colorMaxS=mS;colorMaxB=mB;colorMaxA=mA;}

// Color channel accessors — scaled to current colorMode range
float red(color c)       {unsigned int v=c.value;return (v>>16&0xFF)/255.0f*colorMaxH;}
float green(color c)     {unsigned int v=c.value;return (v>>8&0xFF)/255.0f*colorMaxS;}
float blue(color c)      {unsigned int v=c.value;return (v&0xFF)/255.0f*colorMaxB;}
float alpha(color c)     {unsigned int v=c.value;return (v>>24&0xFF)/255.0f*colorMaxA;}
float brightness(color c){unsigned int v=c.value;float r=(v>>16&0xFF)/255.f,g=(v>>8&0xFF)/255.f,b=(v&0xFF)/255.f;return max(r,max(g,b))*colorMaxB;}
float saturation(color c){unsigned int v=c.value;float r=(v>>16&0xFF)/255.f,g=(v>>8&0xFF)/255.f,b=(v&0xFF)/255.f;float mx=max(r,max(g,b)),mn=min(r,min(g,b));return(mx==0?0:(mx-mn)/mx)*colorMaxS;}
float hue(color c){
    unsigned int v=c.value;
    float r=(v>>16&0xFF)/255.f,g=(v>>8&0xFF)/255.f,b=(v&0xFF)/255.f;
    float mx=max(r,max(g,b)),mn=min(r,min(g,b)),d=mx-mn;
    if(d==0)return 0;
    float h=(mx==r)?(g-b)/d:(mx==g)?2+(b-r)/d:4+(r-g)/d;
    h*=60;if(h<0)h+=360;return h/360.0f*colorMaxH;
}
color lerpColor(color c1,color c2,float t){
    unsigned int v1=c1.value, v2=c2.value;
    return colorVal((int)((v1>>16&0xFF)+(t*((int)(v2>>16&0xFF)-(int)(v1>>16&0xFF)))),
                    (int)((v1>> 8&0xFF)+(t*((int)(v2>>8 &0xFF)-(int)(v1>>8 &0xFF)))),
                    (int)((v1    &0xFF)+(t*((int)(v2    &0xFF)-(int)(v1    &0xFF)))),
                    (int)((v1>>24&0xFF)+(t*((int)(v2>>24&0xFF)-(int)(v1>>24&0xFF)))));
}

// =============================================================================
// INTERNAL HELPERS
// =============================================================================

static void applyFill()  {glColor4f(fillR,fillG,fillB,fillA);}
static void applyStroke(){glColor4f(strokeR,strokeG,strokeB,strokeA);}

static void setProjection(int,int){
    // Always query the actual framebuffer size from GLFW so the
    // viewport covers the full window on every platform/DPI setting.
    // The ortho uses logical winWidth/winHeight so sketch coords are correct.
    int fw = winWidth, fh = winHeight;
    if(gWindow) glfwGetFramebufferSize(gWindow, &fw, &fh);
    glViewport(0, 0, fw, fh);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();
    glOrtho(0, winWidth, winHeight, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
}

static void drawEllipseGeom(float cx,float cy,float rx,float ry,
                             float sa=0,float ea=TWO_PI,int segs=-1){
    if(segs < 0){
        float maxR = rx > ry ? rx : ry;
        segs = (int)(maxR * 2.5f);
        if(segs < 64)  segs = 64;
        if(segs > 512) segs = 512;
    }
    float range=ea-sa;
    bool isFullCircle = (std::fabs(range) >= TWO_PI - 0.001f);
    if(doFill){
        applyFill();
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(cx,cy); // center
        for(int i=0;i<=segs;i++){
            float a=sa+range*i/segs;
            glVertex2f(cx+rx*std::cos(a),cy+ry*std::sin(a));
        }
        if(isFullCircle){
            // close the circle back to the first arc point
            glVertex2f(cx+rx*std::cos(sa),cy+ry*std::sin(sa));
        }
        glEnd();
    }
    if(doStroke){
        applyStroke();glLineWidth(strokeW);
        if(isFullCircle){
            glBegin(GL_LINE_LOOP);
        } else {
            glBegin(GL_LINE_STRIP);
        }
        for(int i=0;i<=segs;i++){
            float a=sa+range*i/segs;
            glVertex2f(cx+rx*std::cos(a),cy+ry*std::sin(a));
        }
        glEnd();
    }
}

static void resolveRect(float& x,float& y,float& w,float& h){
    if(currentRectMode==CENTER_MODE){x-=w*0.5f;y-=h*0.5f;}
    else if(currentRectMode==RADIUS){x-=w;y-=h;w*=2;h*=2;}
    else if(currentRectMode==CORNERS){w=w-x;h=h-y;}
}
static void resolveEllipse(float& cx,float& cy,float& rx,float& ry){
    if(currentEllipseMode==CORNER){cx+=rx;cy+=ry;}
    else if(currentEllipseMode==CORNERS){float ex=rx,ey=ry;rx=(ex-cx)*0.5f;ry=(ey-cy)*0.5f;cx=(cx+ex)*0.5f;cy=(cy+ey)*0.5f;}
    else if(currentEllipseMode==CENTER_MODE){rx*=0.5f;ry*=0.5f;}
}
static void setFillFromColor(color c){unsigned int v=c.value;fillR=(v>>16&0xFF)/255.f;fillG=(v>>8&0xFF)/255.f;fillB=(v&0xFF)/255.f;fillA=(v>>24&0xFF)/255.f;doFill=true;}
static void setStrokeFromColor(color c){unsigned int v=c.value;strokeR=(v>>16&0xFF)/255.f;strokeG=(v>>8&0xFF)/255.f;strokeB=(v&0xFF)/255.f;strokeA=(v>>24&0xFF)/255.f;doStroke=true;}

// =============================================================================
// ENVIRONMENT
// =============================================================================

static bool defaultP3D = false;

void size(int w,int h){
    winWidth=w;winHeight=h;
    pixelWidth=w;pixelHeight=h;
    if(gWindow){
        glfwSetWindowSize(gWindow,w,h);
        glfwPollEvents();
        setProjection(w,h);
    }
}
void size(int w,int h,int renderer){
    defaultP3D=(renderer==P3D);
    size(w,h);
}
void fullScreen(){if(!gWindow){winWidth=displayWidth;winHeight=displayHeight;}else{GLFWmonitor* m=glfwGetPrimaryMonitor();const GLFWvidmode* v=glfwGetVideoMode(m);glfwSetWindowMonitor(gWindow,m,0,0,v->width,v->height,v->refreshRate);}}
void frameRate(int fps){currentFrameRate=fps;targetFrameTime=1.0/fps;}
void settings(){}
void noLoop(){looping=false;}
void loop()  {looping=true;}
void redraw(){redrawOnce=true;}
void exit_sketch(){if(gWindow)glfwSetWindowShouldClose(gWindow,GLFW_TRUE);}
void windowTitle(const std::string& t){if(gWindow)glfwSetWindowTitle(gWindow,t.c_str());}
void windowMove(int x,int y){if(gWindow)glfwSetWindowPos(gWindow,x,y);}
void windowResize(int w,int h){size(w,h);}
void windowResizable(bool r){isResizable=r;if(gWindow)glfwSetWindowAttrib(gWindow,GLFW_RESIZABLE,r?GLFW_TRUE:GLFW_FALSE);}
void windowRatio(int w,int h){if(gWindow)glfwSetWindowAspectRatio(gWindow,w,h);}
void pixelDensity(int d){pixelDensityValue=d;}
void smooth()  {smoothing=true; glEnable(GL_LINE_SMOOTH);glHint(GL_LINE_SMOOTH_HINT,GL_NICEST);glEnable(GL_MULTISAMPLE);}
void noSmooth(){smoothing=false;glDisable(GL_LINE_SMOOTH);glDisable(GL_POLYGON_SMOOTH);}
void hint(int which){
    switch(which){
        case ENABLE_DEPTH_TEST:  glEnable(GL_DEPTH_TEST);  break;
        case DISABLE_DEPTH_TEST: glDisable(GL_DEPTH_TEST); break;
        case ENABLE_DEPTH_SORT:  glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL); break;
        case DISABLE_DEPTH_SORT: glDepthFunc(GL_LESS); break;
        default: break;
    }
}
void cursor()       {if(gWindow)glfwSetInputMode(gWindow,GLFW_CURSOR,GLFW_CURSOR_NORMAL);}
void cursor(int type){if(!gWindow)return;GLFWcursor* c=glfwCreateStandardCursor(type);if(c)glfwSetCursor(gWindow,c);}
void noCursor()     {if(gWindow)glfwSetInputMode(gWindow,GLFW_CURSOR,GLFW_CURSOR_HIDDEN);}

// =============================================================================
// STYLE STACK
// =============================================================================

static void captureStyle(Style& s){s={fillR,fillG,fillB,fillA,strokeR,strokeG,strokeB,strokeA,strokeW,doFill,doStroke,currentRectMode,currentEllipseMode,currentImageMode,tintR,tintG,tintB,tintA,doTint,colorModeVal,colorMaxH,colorMaxS,colorMaxB,colorMaxA};}
static void restoreStyle(const Style& s){fillR=s.fillR;fillG=s.fillG;fillB=s.fillB;fillA=s.fillA;strokeR=s.strokeR;strokeG=s.strokeG;strokeB=s.strokeB;strokeA=s.strokeA;strokeW=s.strokeW;doFill=s.doFill;doStroke=s.doStroke;currentRectMode=s.rectMode;currentEllipseMode=s.ellipseMode;currentImageMode=s.imageMode;tintR=s.tintR;tintG=s.tintG;tintB=s.tintB;tintA=s.tintA;doTint=s.doTint;colorModeVal=s.colorMode;colorMaxH=s.cmH;colorMaxS=s.cmS;colorMaxB=s.cmB;colorMaxA=s.cmA;}
void pushStyle(){Style s;captureStyle(s);styleStack.push_back(s);}
void popStyle() {if(!styleStack.empty()){restoreStyle(styleStack.back());styleStack.pop_back();}}
void push()     {glPushMatrix();pushStyle();}
void pop()      {glPopMatrix(); popStyle();}
void pushMatrix(){glPushMatrix();}
void popMatrix() {glPopMatrix();}

// =============================================================================
// BACKGROUND / CLEAR
// =============================================================================

static void setBg(float r,float g,float b,float a){glClearColor(r,g,b,a);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);}
void background(float gray,float a)           {color c=makeColor(gray,a);unsigned int v=c.value;setBg((v>>16&0xFF)/255.f,(v>>8&0xFF)/255.f,(v&0xFF)/255.f,(v>>24&0xFF)/255.f);}
void background(float r,float g,float b,float a){color c=makeColor(r,g,b,a);unsigned int v=c.value;setBg((v>>16&0xFF)/255.f,(v>>8&0xFF)/255.f,(v&0xFF)/255.f,(v>>24&0xFF)/255.f);}
void background(color c)                      {unsigned int v=c.value;setBg((v>>16&0xFF)/255.f,(v>>8&0xFF)/255.f,(v&0xFF)/255.f,(v>>24&0xFF)/255.f);}
void clear(){glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);}

// =============================================================================
// FILL / STROKE
// =============================================================================

void fill(float gray,float a)              {setFillFromColor(makeColor(gray,a));}
void fill(float r,float g,float b,float a) {setFillFromColor(makeColor(r,g,b,a));}
void fill(color c)                         {setFillFromColor(c);}
void noFill()                              {doFill=false;}
void stroke(float gray,float a)            {setStrokeFromColor(makeColor(gray,a));}
void stroke(float r,float g,float b,float a){setStrokeFromColor(makeColor(r,g,b,a));}
void stroke(color c)                       {setStrokeFromColor(c);}
void noStroke()                            {doStroke=false;}
void strokeWeight(float w)                 {strokeW=w;}
void strokeCap(int)  {}
void strokeJoin(int) {}

// =============================================================================
// PCOLOR CONVENIENCE OVERLOADS
// =============================================================================

void fill(const PColor& c)      { fill(c.r, c.g, c.b, c.a); }
void stroke(const PColor& c)    { stroke(c.r, c.g, c.b, c.a); }
void background(const PColor& c){ background(c.r, c.g, c.b, c.a); }
void tint(const PColor& c)      { tint(c.r, c.g, c.b, c.a); }
void rectMode(int m)    {currentRectMode=m;}
void ellipseMode(int m) {currentEllipseMode=m;}

// =============================================================================
// 2D PRIMITIVES
// =============================================================================

void point(float x,float y){if(!doStroke)return;applyStroke();glPointSize(strokeW);glBegin(GL_POINTS);glVertex2f(x,y);glEnd();}
void line(float x1,float y1,float x2,float y2){if(!doStroke)return;applyStroke();glLineWidth(strokeW);glBegin(GL_LINES);glVertex2f(x1,y1);glVertex2f(x2,y2);glEnd();}
void line(float x1,float y1,float z1,float x2,float y2,float z2){if(!doStroke)return;applyStroke();glLineWidth(strokeW);glBegin(GL_LINES);glVertex3f(x1,y1,z1);glVertex3f(x2,y2,z2);glEnd();}
void ellipse(float cx,float cy,float w,float h){float rx=w,ry=h;resolveEllipse(cx,cy,rx,ry);drawEllipseGeom(cx,cy,rx,ry);}
void circle(float cx,float cy,float d){ellipse(cx,cy,d,d);}
void arc(float cx,float cy,float w,float h,float s,float e){
    // Processing arc() takes width/height as diameter, center as position
    // resolveEllipse handles the mode (CENTER halves w,h into radii)
    float rx=w,ry=h;
    resolveEllipse(cx,cy,rx,ry);
    drawEllipseGeom(cx,cy,rx,ry,s,e);
}
void rect(float x,float y,float w,float h){
    resolveRect(x,y,w,h);
    if(doFill){applyFill();glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+h);glVertex2f(x,y+h);glEnd();}
    if(doStroke){applyStroke();glLineWidth(strokeW);glBegin(GL_LINE_LOOP);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+h);glVertex2f(x,y+h);glEnd();}
}
void rect(float x,float y,float w,float h,float r){
    resolveRect(x,y,w,h);r=min(r,min(w,h)*0.5f);const int sg=8;
    auto corner=[&](float cx,float cy,float sa,float ea){for(int i=0;i<=sg;i++){float a=sa+(ea-sa)*i/sg;glVertex2f(cx+r*std::cos(a),cy+r*std::sin(a));}};
    if(doFill){applyFill();glBegin(GL_TRIANGLE_FAN);glVertex2f(x+w/2,y+h/2);corner(x+r,y+r,PI,PI+HALF_PI);corner(x+w-r,y+r,PI+HALF_PI,TWO_PI);corner(x+w-r,y+h-r,0,HALF_PI);corner(x+r,y+h-r,HALF_PI,PI);glEnd();}
    if(doStroke){applyStroke();glLineWidth(strokeW);glBegin(GL_LINE_LOOP);corner(x+r,y+r,PI,PI+HALF_PI);corner(x+w-r,y+r,PI+HALF_PI,TWO_PI);corner(x+w-r,y+h-r,0,HALF_PI);corner(x+r,y+h-r,HALF_PI,PI);glEnd();}
}
void square(float x,float y,float s){rect(x,y,s,s);}
void triangle(float x1,float y1,float x2,float y2,float x3,float y3){
    if(doFill){applyFill();glBegin(GL_TRIANGLES);glVertex2f(x1,y1);glVertex2f(x2,y2);glVertex2f(x3,y3);glEnd();}
    if(doStroke){applyStroke();glLineWidth(strokeW);glBegin(GL_LINE_LOOP);glVertex2f(x1,y1);glVertex2f(x2,y2);glVertex2f(x3,y3);glEnd();}
}
void quad(float x1,float y1,float x2,float y2,float x3,float y3,float x4,float y4){
    if(doFill){applyFill();glBegin(GL_QUADS);glVertex2f(x1,y1);glVertex2f(x2,y2);glVertex2f(x3,y3);glVertex2f(x4,y4);glEnd();}
    if(doStroke){applyStroke();glLineWidth(strokeW);glBegin(GL_LINE_LOOP);glVertex2f(x1,y1);glVertex2f(x2,y2);glVertex2f(x3,y3);glVertex2f(x4,y4);glEnd();}
}

// =============================================================================
// 3D PRIMITIVES
// =============================================================================

void rotateX(float a){glRotatef(a*180.0f/PI,1,0,0);}
void rotateY(float a){glRotatef(a*180.0f/PI,0,1,0);}
void rotateZ(float a){glRotatef(a*180.0f/PI,0,0,1);}
void sphereDetail(int r){sphereRes=r;}
void box(float s){box(s,s,s);}
void box(float bw,float bh,float bd){
    float hw=bw/2,hh=bh/2,hd=bd/2;
    struct Face{ float nx,ny,nz; float v[4][3]; };
    Face faces[]={
        { 0, 0, 1,{{-hw,-hh,hd},{hw,-hh,hd},{hw,hh,hd},{-hw,hh,hd}}},
        { 0, 0,-1,{{hw,-hh,-hd},{-hw,-hh,-hd},{-hw,hh,-hd},{hw,hh,-hd}}},
        {-1, 0, 0,{{-hw,-hh,-hd},{-hw,-hh,hd},{-hw,hh,hd},{-hw,hh,-hd}}},
        { 1, 0, 0,{{hw,-hh,hd},{hw,-hh,-hd},{hw,hh,-hd},{hw,hh,hd}}},
        { 0,-1, 0,{{-hw,-hh,-hd},{hw,-hh,-hd},{hw,-hh,hd},{-hw,-hh,hd}}},
        { 0, 1, 0,{{-hw,hh,hd},{hw,hh,hd},{hw,hh,-hd},{-hw,hh,-hd}}},
    };
    if(doFill){
        applyFill();
        glBegin(GL_QUADS);
        for(auto& f:faces){
            glNormal3f(f.nx,f.ny,f.nz);
            for(auto& v:f.v) glVertex3f(v[0],v[1],v[2]);
        }
        glEnd();
    }
    if(doStroke){
        applyStroke();glLineWidth(strokeW);
        float vx[]={-hw,-hw,hw,hw,-hw,-hw,hw,hw};
        float vy[]={-hh,hh,hh,-hh,-hh,hh,hh,-hh};
        float vz[]={hd,hd,hd,hd,-hd,-hd,-hd,-hd};
        int e[][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
        glBegin(GL_LINES);
        for(auto& ee:e){glVertex3f(vx[ee[0]],vy[ee[0]],vz[ee[0]]);glVertex3f(vx[ee[1]],vy[ee[1]],vz[ee[1]]);}
        glEnd();
    }
}
void sphere(float r){
    int stacks=sphereRes, slices=sphereRes;
    if(doFill){
        for(int i=0;i<stacks;i++){
            float a0=PI*i/stacks-HALF_PI, a1=PI*(i+1)/stacks-HALF_PI;
            applyFill();
            glBegin(GL_QUAD_STRIP);
            for(int j=0;j<=slices;j++){
                float b=TWO_PI*j/slices;
                float x1=std::cos(a1)*std::cos(b),y1=std::sin(a1),z1=std::cos(a1)*std::sin(b);
                float x0=std::cos(a0)*std::cos(b),y0=std::sin(a0),z0=std::cos(a0)*std::sin(b);
                glNormal3f(x1,y1,z1); glVertex3f(r*x1,r*y1,r*z1);
                glNormal3f(x0,y0,z0); glVertex3f(r*x0,r*y0,r*z0);
            }
            glEnd();
        }
    }
    if(doStroke && !doFill){
        applyStroke(); glLineWidth(strokeW);
        for(int i=0;i<=stacks;i++){
            float lat=PI*(-0.5f+(float)i/stacks);
            float y0=r*std::sin(lat), rc=r*std::cos(lat);
            glBegin(GL_LINE_LOOP);
            for(int j=0;j<slices;j++){
                float lng=TWO_PI*(float)j/slices;
                glVertex3f(rc*std::cos(lng), y0, rc*std::sin(lng));
            }
            glEnd();
        }
        for(int j=0;j<slices;j++){
            float lng=TWO_PI*(float)j/slices;
            glBegin(GL_LINE_STRIP);
            for(int i=0;i<=stacks;i++){
                float lat=PI*(-0.5f+(float)i/stacks);
                glVertex3f(r*std::cos(lat)*std::cos(lng),
                           r*std::sin(lat),
                           r*std::cos(lat)*std::sin(lng));
            }
            glEnd();
        }
    }
}

// =============================================================================
// VERTEX / SHAPES
// =============================================================================

void beginShape(int kind){
    shapeKind=kind;inShape=true;shapeVerts.clear();
    // Always collect verts — used for stroke outlines in all modes.
    // For explicit kinds we also open a glBegin for immediate fill rendering.
    shape3D=false;
    if(kind!=-1){
        GLenum gm;
        switch(kind){
            case POINTS:gm=GL_POINTS;break;case LINES:gm=GL_LINES;break;
            case TRIANGLES:gm=GL_TRIANGLES;break;case TRIANGLE_FAN:gm=GL_TRIANGLE_FAN;break;
            case TRIANGLE_STRIP:gm=GL_TRIANGLE_STRIP;break;case QUADS:gm=GL_QUADS;break;
            case QUAD_STRIP:gm=GL_QUAD_STRIP;break;default:gm=GL_TRIANGLES;break;
        }
        shape3D=true;  // signals endShape to close the glBegin
        if(doFill){
            // Push fill back slightly so stroke lines drawn at same z are visible
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 1.0f);
            applyFill();
            glBegin(gm);
        }
    }
}
void vertex(float x,float y){
    if(inContour){contourVerts.push_back({x,y});return;}
    if(!inShape)return;
    shapeVerts.push_back({x,y});   // always collect
    if(shape3D) glVertex3f(x,y,0); // also emit immediately for explicit kinds
}
void vertex(float x,float y,float z){
    if(inContour){contourVerts.push_back({x,y});return;}
    if(!inShape)return;
    shapeVerts.push_back({x,y});   // always collect 2D projection for stroke
    if(shape3D) glVertex3f(x,y,z); // also emit immediately for explicit kinds
}
void beginContour(){inContour=true;contourVerts.clear();}
void endContour()  {inContour=false;}

void endShape(int mode){
    if(!inShape){return;}
    bool cl=(mode==CLOSE);
    if(shape3D){
        // Close the immediate-mode fill pass
        if(doFill){
            glEnd();
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
        // Draw stroke outlines — shapeVerts was collected in vertex()
        if(doStroke && !shapeVerts.empty()){
            applyStroke(); glLineWidth(strokeW);
            int n=(int)shapeVerts.size();
            auto lv=[&](int i)->std::pair<float,float>{return shapeVerts[i];};
            switch(shapeKind){
                case TRIANGLE_STRIP:
                    glBegin(GL_LINES);
                    for(int i=0;i+2<n;i++){
                        auto a=lv(i),b=lv(i+1),c=lv(i+2);
                        glVertex2f(a.first,a.second);glVertex2f(b.first,b.second);
                        glVertex2f(b.first,b.second);glVertex2f(c.first,c.second);
                        glVertex2f(c.first,c.second);glVertex2f(a.first,a.second);
                    }
                    glEnd();
                    break;
                case TRIANGLE_FAN:
                    glBegin(GL_LINES);
                    for(int i=1;i+1<n;i++){
                        auto a=lv(0),b=lv(i),c=lv(i+1);
                        glVertex2f(a.first,a.second);glVertex2f(b.first,b.second);
                        glVertex2f(b.first,b.second);glVertex2f(c.first,c.second);
                        glVertex2f(c.first,c.second);glVertex2f(a.first,a.second);
                    }
                    glEnd();
                    break;
                case TRIANGLES:
                    glBegin(GL_LINES);
                    for(int i=0;i+2<n;i+=3){
                        auto a=lv(i),b=lv(i+1),c=lv(i+2);
                        glVertex2f(a.first,a.second);glVertex2f(b.first,b.second);
                        glVertex2f(b.first,b.second);glVertex2f(c.first,c.second);
                        glVertex2f(c.first,c.second);glVertex2f(a.first,a.second);
                    }
                    glEnd();
                    break;
                case QUADS:
                    glBegin(GL_LINES);
                    for(int i=0;i+3<n;i+=4){
                        auto a=lv(i),b=lv(i+1),c=lv(i+2),d=lv(i+3);
                        glVertex2f(a.first,a.second);glVertex2f(b.first,b.second);
                        glVertex2f(b.first,b.second);glVertex2f(c.first,c.second);
                        glVertex2f(c.first,c.second);glVertex2f(d.first,d.second);
                        glVertex2f(d.first,d.second);glVertex2f(a.first,a.second);
                    }
                    glEnd();
                    break;
                case QUAD_STRIP:
                    glBegin(GL_LINES);
                    for(int i=0;i+3<n;i+=2){
                        auto a=lv(i),b=lv(i+1),c=lv(i+3),d=lv(i+2);
                        glVertex2f(a.first,a.second);glVertex2f(b.first,b.second);
                        glVertex2f(b.first,b.second);glVertex2f(c.first,c.second);
                        glVertex2f(c.first,c.second);glVertex2f(d.first,d.second);
                        glVertex2f(d.first,d.second);glVertex2f(a.first,a.second);
                    }
                    glEnd();
                    break;
                default:
                    glBegin(cl?GL_LINE_LOOP:GL_LINE_STRIP);
                    for(auto& v:shapeVerts)glVertex2f(v.first,v.second);
                    glEnd();
                    break;
            }
        }
        inShape=false;shape3D=false;shapeVerts.clear();
        return;
    }
    if(shapeVerts.empty()){inShape=false;return;}

    // Default beginShape() / endShape(CLOSE) path.
    // Draw as GL_TRIANGLES from center to each edge — no stencil needed.
    // This is correct for any star or polygon because we explicitly
    // triangulate: for each edge (v[i], v[i+1]) draw (center, v[i], v[i+1]).
    if(shapeKind==-1 || shapeKind==CLOSE){
        if(doFill && shapeVerts.size()>=3){
            int n = (int)shapeVerts.size();

            // Use the actual geometric center of the shape
            float cx=0, cy=0;
            for(auto& v:shapeVerts){ cx+=v.first; cy+=v.second; }
            cx/=n; cy/=n;

            glDisable(GL_CULL_FACE);
            applyFill();
            glBegin(GL_TRIANGLES);
            for(int i=0;i<n;i++){
                int j=(i+1)%n;
                glVertex2f(cx, cy);
                glVertex2f(shapeVerts[i].first,  shapeVerts[i].second);
                glVertex2f(shapeVerts[j].first,  shapeVerts[j].second);
            }
            glEnd();
        }
        if(doStroke){
            applyStroke();glLineWidth(strokeW);
            glBegin(cl?GL_LINE_LOOP:GL_LINE_STRIP);
            for(auto& v:shapeVerts)glVertex2f(v.first,v.second);
            glEnd();
        }
    }
    inShape=false;shapeVerts.clear();
}

void bezierVertex(float cx1,float cy1,float cx2,float cy2,float x,float y){
    if(!inShape||shapeVerts.empty())return;
    auto[x0,y0]=shapeVerts.back();const int sg=bezierDetailVal;
    for(int i=1;i<=sg;i++){float t=i/(float)sg,u=1-t;
        shapeVerts.push_back({u*u*u*x0+3*u*u*t*cx1+3*u*t*t*cx2+t*t*t*x,u*u*u*y0+3*u*u*t*cy1+3*u*t*t*cy2+t*t*t*y});}
}
void quadraticVertex(float cx,float cy,float x,float y){
    if(!inShape||shapeVerts.empty())return;
    auto[x0,y0]=shapeVerts.back();const int sg=bezierDetailVal;
    for(int i=1;i<=sg;i++){float t=i/(float)sg,u=1-t;shapeVerts.push_back({u*u*x0+2*u*t*cx+t*t*x,u*u*y0+2*u*t*cy+t*t*y});}
}
void curveVertex(float x,float y){if(inShape)shapeVerts.push_back({x,y});}

void bezier(float x1,float y1,float cx1,float cy1,float cx2,float cy2,float x2,float y2){
    if(!doStroke)return;applyStroke();glLineWidth(strokeW);glBegin(GL_LINE_STRIP);
    for(int i=0;i<=bezierDetailVal;i++){float t=i/(float)bezierDetailVal,u=1-t;
        glVertex2f(u*u*u*x1+3*u*u*t*cx1+3*u*t*t*cx2+t*t*t*x2,u*u*u*y1+3*u*u*t*cy1+3*u*t*t*cy2+t*t*t*y2);}
    glEnd();
}
void curve(float x0,float y0,float x1,float y1,float x2,float y2,float x3,float y3){
    if(!doStroke)return;applyStroke();glLineWidth(strokeW);glBegin(GL_LINE_STRIP);
    float s=curveTightnessVal;
    for(int i=0;i<=curveDetailVal;i++){
        float t=i/(float)curveDetailVal,t2=t*t,t3=t2*t;
        float b0=(-s*t3+2*s*t2-s*t)/2.f,b1=((2-s)*t3+(s-3)*t2+1)/2.f;
        float b2=((s-2)*t3+(3-2*s)*t2+s*t)/2.f,b3=(s*t3-s*t2)/2.f;
        glVertex2f(b0*x0+b1*x1+b2*x2+b3*x3,b0*y0+b1*y1+b2*y2+b3*y3);
    }
    glEnd();
}
float bezierPoint(float a,float b,float c,float d,float t){float u=1-t;return u*u*u*a+3*u*u*t*b+3*u*t*t*c+t*t*t*d;}
float bezierTangent(float a,float b,float c,float d,float t){float u=1-t;return 3*u*u*(b-a)+6*u*t*(c-b)+3*t*t*(d-c);}
float curvePoint(float a,float b,float c,float d,float t){float t2=t*t,t3=t2*t,s=curveTightnessVal;return 0.5f*((-s*t3+2*s*t2-s*t)*a+((2-s)*t3+(s-3)*t2+1)*b+((s-2)*t3+(3-2*s)*t2+s*t)*c+(s*t3-s*t2)*d);}
float curveTangent(float a,float b,float c,float d,float t){float t2=t*t,s=curveTightnessVal;return 0.5f*((-3*s*t2+4*s*t-s)*a+(3*(2-s)*t2+2*(s-3)*t)*b+(3*(s-2)*t2+2*(3-2*s)*t+s)*c+(3*s*t2-2*s*t)*d);}
void curveDetail(int d)     {curveDetailVal=d;}
void curveTightness(float t){curveTightnessVal=t;}
void bezierDetail(int d)    {bezierDetailVal=d;}

// =============================================================================
// MATRIX
// =============================================================================

void resetMatrix(){glLoadIdentity();}
void applyMatrix(float n00,float n01,float n02,float n03,float n10,float n11,float n12,float n13,float n20,float n21,float n22,float n23,float n30,float n31,float n32,float n33){
    float m[]={n00,n10,n20,n30,n01,n11,n21,n31,n02,n12,n22,n32,n03,n13,n23,n33};
    glMultMatrixf(m);
}
void translate(float x,float y)        {glTranslatef(x,y,0);}
void translate(float x,float y,float z){glTranslatef(x,y,z);}
void scale(float s)                    {glScalef(s,s,1);}
void scale(float sx,float sy)          {glScalef(sx,sy,1);}
void rotate(float a)                   {glRotatef(a*180.0f/PI,0,0,1);}
void shearX(float a)                   {float m[]={1,0,0,0,std::tan(a),1,0,0,0,0,1,0,0,0,0,1};glMultMatrixf(m);}
void shearY(float a)                   {float m[]={1,std::tan(a),0,0,0,1,0,0,0,0,1,0,0,0,0,1};glMultMatrixf(m);}
void printMatrix(){float m[16];glGetFloatv(GL_MODELVIEW_MATRIX,m);for(int i=0;i<4;i++){for(int j=0;j<4;j++)std::cout<<m[j*4+i]<<" ";std::cout<<"\n";}}

static void projectPoint(float x,float y,float z,float& ox,float& oy,float& oz){
    float mv[16],proj[16];int vp[4];
    glGetFloatv(GL_MODELVIEW_MATRIX,mv);glGetFloatv(GL_PROJECTION_MATRIX,proj);glGetIntegerv(GL_VIEWPORT,vp);
    float cx=mv[0]*x+mv[4]*y+mv[8]*z+mv[12];
    float cy=mv[1]*x+mv[5]*y+mv[9]*z+mv[13];
    float cz=mv[2]*x+mv[6]*y+mv[10]*z+mv[14];
    float cw=mv[3]*x+mv[7]*y+mv[11]*z+mv[15];
    float px=proj[0]*cx+proj[4]*cy+proj[8]*cz+proj[12]*cw;
    float py=proj[1]*cx+proj[5]*cy+proj[9]*cz+proj[13]*cw;
    float pz=proj[2]*cx+proj[6]*cy+proj[10]*cz+proj[14]*cw;
    float pw=proj[3]*cx+proj[7]*cy+proj[11]*cz+proj[15]*cw;
    if(pw!=0){px/=pw;py/=pw;pz/=pw;}
    ox=vp[0]+(px+1)*0.5f*vp[2];
    oy=vp[1]+(1-py)*0.5f*vp[3];
    oz=(pz+1)*0.5f;
}
float screenX(float x,float y,float z){float ox,oy,oz;projectPoint(x,y,z,ox,oy,oz);return ox;}
float screenY(float x,float y,float z){float ox,oy,oz;projectPoint(x,y,z,ox,oy,oz);return oy;}
float screenZ(float x,float y,float z){float ox,oy,oz;projectPoint(x,y,z,ox,oy,oz);return oz;}
float modelX(float x,float y,float z) {float mv[16];glGetFloatv(GL_MODELVIEW_MATRIX,mv);return mv[0]*x+mv[4]*y+mv[8]*z+mv[12];}
float modelY(float x,float y,float z) {float mv[16];glGetFloatv(GL_MODELVIEW_MATRIX,mv);return mv[1]*x+mv[5]*y+mv[9]*z+mv[13];}
float modelZ(float x,float y,float z) {float mv[16];glGetFloatv(GL_MODELVIEW_MATRIX,mv);return mv[2]*x+mv[6]*y+mv[10]*z+mv[14];}

// =============================================================================
// CAMERA
// =============================================================================

// Internal helper — sets up the standard Processing Y-flipped perspective
// camera. Called by camera() and perspective() so they stay in sync.
static void applyDefaultCamera(){
    float eyeZ = ((float)winHeight/2.0f) / std::tan(PI*60.0f/360.0f);
    float near_ = eyeZ/10.0f, far_ = eyeZ*10.0f;
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective(60.0, (double)winWidth/winHeight, near_, far_);
    glScalef(1,-1,1);   // flip Y so Processing's Y-down coords work
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    gluLookAt(winWidth/2.0, winHeight/2.0, eyeZ,
              winWidth/2.0, winHeight/2.0, 0,
              0, 1, 0);
    glFrontFace(GL_CW);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
}

void camera(){
    applyDefaultCamera();
}
void camera(float ex,float ey,float ez,float cx,float cy,float cz,float ux,float uy,float uz){
    // Custom camera — keep the same Y-flip projection, just change the view
    float eyeZ = ((float)winHeight/2.0f) / std::tan(PI*60.0f/360.0f);
    float near_ = eyeZ/10.0f, far_ = eyeZ*10.0f;
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective(60.0,(double)winWidth/winHeight,near_,far_);
    glScalef(1,-1,1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    gluLookAt(ex,ey,ez,cx,cy,cz,ux,uy,uz);
    glFrontFace(GL_CW);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}
void beginCamera(){glMatrixMode(GL_MODELVIEW);glPushMatrix();}
void endCamera()  {glPopMatrix();}

void perspective(){
    applyDefaultCamera();
}
void perspective(float fov,float aspect,float zNear,float zFar){
    // User-supplied perspective — apply Y-flip so coords stay consistent
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective(degrees(fov), aspect, zNear, zFar);
    glScalef(1,-1,1);
    // Re-apply the default modelview camera so translate/rotate work correctly
    float eyeZ = ((float)winHeight/2.0f) / std::tan(PI*60.0f/360.0f);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    gluLookAt(winWidth/2.0, winHeight/2.0, eyeZ,
              winWidth/2.0, winHeight/2.0, 0,
              0, 1, 0);
    glFrontFace(GL_CW);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
}
void ortho(){
    glMatrixMode(GL_PROJECTION);glLoadIdentity();
    glOrtho(0,winWidth,winHeight,0,-winHeight,winHeight);
    glMatrixMode(GL_MODELVIEW);
}
void ortho(float l,float r,float b,float t,float n,float f){
    glMatrixMode(GL_PROJECTION);glLoadIdentity();
    glOrtho(l,r,b,t,n,f);
    glMatrixMode(GL_MODELVIEW);
}
void frustum(float l,float r,float b,float t,float n,float f){
    glMatrixMode(GL_PROJECTION);glLoadIdentity();
    glFrustum(l,r,b,t,n,f);
    glMatrixMode(GL_MODELVIEW);
}
void printCamera(){float m[16];glGetFloatv(GL_MODELVIEW_MATRIX,m);std::cout<<"Camera matrix:\n";for(int i=0;i<4;i++){for(int j=0;j<4;j++)std::cout<<m[j*4+i]<<" ";std::cout<<"\n";}}
void printProjection(){float m[16];glGetFloatv(GL_PROJECTION_MATRIX,m);std::cout<<"Projection matrix:\n";for(int i=0;i<4;i++){for(int j=0;j<4;j++)std::cout<<m[j*4+i]<<" ";std::cout<<"\n";}}

// =============================================================================
// LIGHTS
// =============================================================================

void lights(){
    glEnable(GL_LIGHTING);
    glEnable(GL_NORMALIZE);
    // GL_COLOR_MATERIAL: glColor4f() drives the material diffuse+ambient
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    lightsEnabled=true; lightIndex=0;

    // Processing Java lights() defaults:
    //   ambient  ~21%  (53,53,53)
    //   diffuse  ~80%  (204,204,204)
    //   direction: from upper-left-front
    // GL_LIGHT_MODEL_AMBIENT controls the scene-wide ambient independent of lights
    GLfloat globalAmb[]={0.212f, 0.212f, 0.212f, 1.0f}; // 54/255
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, globalAmb);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE);

    // No per-light ambient — handled by global above
    GLfloat amb[]={0.0f, 0.0f, 0.0f, 1.0f};
    GLfloat dif[]={0.8f, 0.8f, 0.8f, 1.0f};
    GLfloat spc[]={0.0f, 0.0f, 0.0f, 1.0f};
    // Directional: w=0, vector points TO the light source
    // Processing Java uses direction (0,0,-1) toward viewer,
    // combined with the Y-flipped camera this gives upper-front shading
    GLfloat pos[]={-1.0f, -1.0f, 1.0f, 0.0f};

    glEnable(GL_LIGHT0);
    glLightfv(GL_LIGHT0, GL_AMBIENT,  amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  dif);
    glLightfv(GL_LIGHT0, GL_SPECULAR, spc);
    // Position must be set while modelview = camera (eye space) so the
    // direction is fixed relative to the viewer, not the world
    glPushMatrix();
    glLoadIdentity();
    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glPopMatrix();
    lightIndex=1;
}
void noLights(){
    glDisable(GL_LIGHTING);
    glDisable(GL_COLOR_MATERIAL);
    lightsEnabled=false;lightIndex=0;
}
void ambientLight(float r,float g,float b){ambientLight(r,g,b,0,0,0);}
void ambientLight(float r,float g,float b,float x,float y,float z){
    if(lightIndex>=8)return;GLenum lt=GL_LIGHT0+lightIndex++;
    GLfloat col[]={r,g,b,1},pos[]={x,y,z,1};
    glEnable(GL_LIGHTING);glEnable(lt);
    glLightfv(lt,GL_AMBIENT,col);glLightfv(lt,GL_POSITION,pos);
}
void directionalLight(float r,float g,float b,float nx,float ny,float nz){
    if(lightIndex>=8)return;GLenum lt=GL_LIGHT0+lightIndex++;
    GLfloat col[]={r,g,b,1},pos[]={-nx,-ny,-nz,0};
    glEnable(GL_LIGHTING);glEnable(lt);
    glLightfv(lt,GL_DIFFUSE,col);glLightfv(lt,GL_POSITION,pos);
}
void pointLight(float r,float g,float b,float x,float y,float z){
    if(lightIndex>=8)return;GLenum lt=GL_LIGHT0+lightIndex++;
    GLfloat col[]={r,g,b,1},pos[]={x,y,z,1};
    glEnable(GL_LIGHTING);glEnable(lt);
    glLightfv(lt,GL_DIFFUSE,col);glLightfv(lt,GL_POSITION,pos);
}
void spotLight(float r,float g,float b,float x,float y,float z,float nx,float ny,float nz,float angle,float conc){
    if(lightIndex>=8)return;GLenum lt=GL_LIGHT0+lightIndex++;
    GLfloat col[]={r,g,b,1},pos[]={x,y,z,1},dir[]={nx,ny,nz};
    glEnable(GL_LIGHTING);glEnable(lt);
    glLightfv(lt,GL_DIFFUSE,col);glLightfv(lt,GL_POSITION,pos);
    glLightfv(lt,GL_SPOT_DIRECTION,dir);
    glLightf(lt,GL_SPOT_CUTOFF,angle*180.0f/PI);
    glLightf(lt,GL_SPOT_EXPONENT,conc);
}
void lightFalloff(float c,float l,float q){
    for(int i=0;i<lightIndex;i++){GLenum lt=GL_LIGHT0+i;glLightf(lt,GL_CONSTANT_ATTENUATION,c);glLightf(lt,GL_LINEAR_ATTENUATION,l);glLightf(lt,GL_QUADRATIC_ATTENUATION,q);}
}
void lightSpecular(float r,float g,float b){
    for(int i=0;i<lightIndex;i++){GLenum lt=GL_LIGHT0+i;GLfloat col[]={r,g,b,1};glLightfv(lt,GL_SPECULAR,col);}
}
void normal(float nx,float ny,float nz){glNormal3f(nx,ny,nz);}

// =============================================================================
// MATERIAL
// =============================================================================

void ambient(float r,float g,float b) {GLfloat c[]={r,g,b,1};glMaterialfv(GL_FRONT_AND_BACK,GL_AMBIENT,c);}
void ambient(color c)                 {unsigned int v=c.value;ambient((v>>16&0xFF)/255.f,(v>>8&0xFF)/255.f,(v&0xFF)/255.f);}
void emissive(float r,float g,float b){GLfloat c[]={r,g,b,1};glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,c);}
void emissive(color c)                {unsigned int v=c.value;emissive((v>>16&0xFF)/255.f,(v>>8&0xFF)/255.f,(v&0xFF)/255.f);}
void specular(float r,float g,float b){GLfloat c[]={r,g,b,1};glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,c);}
void specular(color c)                {unsigned int v=c.value;specular((v>>16&0xFF)/255.f,(v>>8&0xFF)/255.f,(v&0xFF)/255.f);}
void shininess(float s)               {glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,s);}

// =============================================================================
// TEXT
// =============================================================================

// ── Embedded 6×8 bitmap font fallback (ASCII 32–126) ─────────────────────────
static const unsigned char g_font6x8[][6] = {
    {0x00,0x00,0x00,0x00,0x00,0x00},{0x04,0x04,0x04,0x04,0x00,0x04},{0x0A,0x0A,0x00,0x00,0x00,0x00},{0x0A,0x1F,0x0A,0x1F,0x0A,0x00},{0x0E,0x15,0x1C,0x07,0x15,0x0E},{0x19,0x1A,0x02,0x04,0x0B,0x13},{0x08,0x14,0x08,0x15,0x12,0x0D},{0x04,0x04,0x00,0x00,0x00,0x00},
    {0x02,0x04,0x04,0x04,0x04,0x02},{0x08,0x04,0x04,0x04,0x04,0x08},{0x00,0x0A,0x04,0x1F,0x04,0x0A},{0x00,0x04,0x04,0x1F,0x04,0x04},{0x00,0x00,0x00,0x00,0x04,0x08},{0x00,0x00,0x00,0x1F,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x04},{0x01,0x02,0x02,0x04,0x08,0x10},
    {0x0E,0x11,0x13,0x15,0x19,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x0E},{0x0E,0x11,0x02,0x04,0x08,0x1F},{0x1F,0x02,0x06,0x01,0x11,0x0E},{0x02,0x06,0x0A,0x1F,0x02,0x02},{0x1F,0x10,0x1E,0x01,0x11,0x0E},{0x06,0x08,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x04,0x04},
    {0x0E,0x11,0x0E,0x11,0x11,0x0E},{0x0E,0x11,0x0F,0x01,0x02,0x0C},{0x00,0x04,0x00,0x00,0x04,0x00},{0x00,0x04,0x00,0x00,0x04,0x08},{0x02,0x04,0x08,0x08,0x04,0x02},{0x00,0x1F,0x00,0x1F,0x00,0x00},{0x08,0x04,0x02,0x02,0x04,0x08},{0x0E,0x11,0x02,0x04,0x00,0x04},
    {0x0E,0x11,0x17,0x15,0x17,0x0E},{0x04,0x0A,0x11,0x1F,0x11,0x11},{0x1E,0x11,0x1E,0x11,0x11,0x1E},{0x0E,0x11,0x10,0x10,0x11,0x0E},{0x1C,0x12,0x11,0x11,0x12,0x1C},{0x1F,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x1E,0x10,0x10,0x10},{0x0E,0x11,0x10,0x17,0x11,0x0F},
    {0x11,0x11,0x1F,0x11,0x11,0x11},{0x0E,0x04,0x04,0x04,0x04,0x0E},{0x07,0x02,0x02,0x02,0x12,0x0C},{0x11,0x12,0x1C,0x12,0x11,0x11},{0x10,0x10,0x10,0x10,0x10,0x1F},{0x11,0x1B,0x15,0x11,0x11,0x11},{0x11,0x19,0x15,0x13,0x11,0x11},{0x0E,0x11,0x11,0x11,0x11,0x0E},
    {0x1E,0x11,0x11,0x1E,0x10,0x10},{0x0E,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x11,0x11},{0x0F,0x10,0x0E,0x01,0x11,0x0E},{0x1F,0x04,0x04,0x04,0x04,0x04},{0x11,0x11,0x11,0x11,0x11,0x0E},{0x11,0x11,0x11,0x0A,0x0A,0x04},{0x11,0x11,0x15,0x15,0x1B,0x11},
    {0x11,0x0A,0x04,0x04,0x0A,0x11},{0x11,0x0A,0x04,0x04,0x04,0x04},{0x1F,0x02,0x04,0x08,0x10,0x1F},{0x0E,0x08,0x08,0x08,0x08,0x0E},{0x10,0x08,0x08,0x04,0x02,0x01},{0x0E,0x02,0x02,0x02,0x02,0x0E},{0x04,0x0A,0x11,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x1F},
    {0x08,0x04,0x00,0x00,0x00,0x00},{0x00,0x0E,0x01,0x0F,0x11,0x0F},{0x10,0x10,0x1E,0x11,0x11,0x1E},{0x00,0x0E,0x10,0x10,0x10,0x0E},{0x01,0x01,0x0F,0x11,0x11,0x0F},{0x00,0x0E,0x11,0x1F,0x10,0x0E},{0x06,0x08,0x1E,0x08,0x08,0x08},{0x00,0x0F,0x11,0x0F,0x01,0x0E},
    {0x10,0x10,0x1E,0x11,0x11,0x11},{0x04,0x00,0x04,0x04,0x04,0x0E},{0x02,0x00,0x02,0x02,0x12,0x0C},{0x10,0x12,0x14,0x1C,0x12,0x11},{0x0C,0x04,0x04,0x04,0x04,0x0E},{0x00,0x1B,0x15,0x15,0x11,0x11},{0x00,0x1E,0x11,0x11,0x11,0x11},{0x00,0x0E,0x11,0x11,0x11,0x0E},
    {0x00,0x1E,0x11,0x1E,0x10,0x10},{0x00,0x0F,0x11,0x0F,0x01,0x01},{0x00,0x16,0x19,0x10,0x10,0x10},{0x00,0x0E,0x10,0x0E,0x01,0x1E},{0x08,0x1F,0x08,0x08,0x08,0x07},{0x00,0x11,0x11,0x11,0x13,0x0D},{0x00,0x11,0x11,0x0A,0x0A,0x04},{0x00,0x11,0x15,0x15,0x1B,0x11},
    {0x00,0x11,0x0A,0x04,0x0A,0x11},{0x00,0x11,0x0A,0x04,0x08,0x10},{0x00,0x1F,0x02,0x04,0x08,0x1F},{0x06,0x04,0x0C,0x04,0x04,0x06},{0x04,0x04,0x04,0x04,0x04,0x04},{0x0C,0x04,0x06,0x04,0x04,0x0C},{0x08,0x15,0x02,0x00,0x00,0x00},
};

// ── stb_truetype font state ───────────────────────────────────────────────────
#if PROCESSING_HAS_STB_TRUETYPE
struct TTFFont {
    stbtt_fontinfo info;
    std::vector<unsigned char> data;
    GLuint texID = 0;
    // Baked atlas: ASCII 32-126
    stbtt_bakedchar chars[96];
    int atlasW = 512, atlasH = 512;
    float bakeSize = 0.0f;   // size the atlas was baked at
    bool  loaded   = false;
};
static TTFFont g_ttf;

static bool loadTTFFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    g_ttf.data = std::vector<unsigned char>(
        (std::istreambuf_iterator<char>(f)),
         std::istreambuf_iterator<char>());
    return stbtt_InitFont(&g_ttf.info, g_ttf.data.data(), 0) != 0;
}

static void bakeAtlas(float pixelSize) {
    if (!g_ttf.loaded) return;
    if (std::fabs(pixelSize - g_ttf.bakeSize) < 0.5f) return; // already baked at this size
    g_ttf.bakeSize = pixelSize;
    std::vector<unsigned char> bitmap(g_ttf.atlasW * g_ttf.atlasH);
    stbtt_BakeFontBitmap(g_ttf.data.data(), 0, pixelSize,
                         bitmap.data(), g_ttf.atlasW, g_ttf.atlasH,
                         32, 96, g_ttf.chars);
    // Upload as alpha-only texture
    if (g_ttf.texID == 0) glGenTextures(1, &g_ttf.texID);
    glBindTexture(GL_TEXTURE_2D, g_ttf.texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, g_ttf.atlasW, g_ttf.atlasH,
                 0, GL_ALPHA, GL_UNSIGNED_BYTE, bitmap.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}
#endif // PROCESSING_HAS_STB_TRUETYPE

// ── Shared text state ─────────────────────────────────────────────────────────
static float g_textSize    = 14.0f;
static int   g_textAlignX  = LEFT_ALIGN;
static int   g_textAlignY  = BASELINE;
static float g_textLeading = 0.0f;

// ── Bitmap font rendering (fallback) ─────────────────────────────────────────
static const int BF_GW = 6;
static const int BF_GH = 8;

static void drawBitmapStr(float x, float y, const std::string& s, int scale) {
    glColor4f(fillR, fillG, fillB, fillA);
    glDisable(GL_TEXTURE_2D);
    float cx = x;
    for (char ch : s) {
        if (ch < 32 || ch > 126) { cx += (BF_GW+1)*scale; continue; }
        const unsigned char* bm = g_font6x8[(unsigned char)ch - 32];
        for (int row = 0; row < BF_GH; row++) {
            unsigned char bits = bm[row];
            for (int col = 0; col < BF_GW; col++) {
                if (bits & (1 << (BF_GW - 1 - col))) {
                    glBegin(GL_QUADS);
                    float px = cx + col*scale, py = y + row*scale;
                    glVertex2f(px,        py);
                    glVertex2f(px+scale,  py);
                    glVertex2f(px+scale,  py+scale);
                    glVertex2f(px,        py+scale);
                    glEnd();
                }
            }
        }
        cx += (BF_GW+1)*scale;
    }
}

static float bitmapStrWidth(const std::string& s, int scale) {
    return s.size() * (BF_GW+1) * scale;
}

// ── TTF rendering ─────────────────────────────────────────────────────────────
#if PROCESSING_HAS_STB_TRUETYPE
static float ttfStrWidth(const std::string& s) {
    float x = 0;
    for (char ch : s) {
        if (ch < 32 || ch > 127) continue;
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&g_ttf.info, ch, &advance, &lsb);
        float sc = stbtt_ScaleForPixelHeight(&g_ttf.info, g_textSize);
        x += advance * sc;
    }
    return x;
}

static void drawTTFStr(float x, float y, const std::string& s) {
    if (!g_ttf.loaded) return;
    bakeAtlas(g_textSize);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_ttf.texID);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Use fill colour modulated by font alpha
    glColor4f(fillR, fillG, fillB, fillA);
    glBegin(GL_QUADS);

    float cx = x;
    for (char ch : s) {
        if (ch < 32 || ch >= 128) continue;
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(g_ttf.chars, g_ttf.atlasW, g_ttf.atlasH,
                           ch - 32, &cx, &y, &q, 1);
        glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, q.y0);
        glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, q.y0);
        glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, q.y1);
        glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, q.y1);
    }
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}
#endif

// ── Main renderText entry point ───────────────────────────────────────────────
static float getLineWidth(const std::string& line) {
#if PROCESSING_HAS_STB_TRUETYPE
    if (g_ttf.loaded) return ttfStrWidth(line);
#endif
    int sc = std::max(1,(int)(g_textSize/8.0f));
    return bitmapStrWidth(line, sc);
}

static void renderText(const std::string& msg, float x, float y) {
    if (msg.empty()) return;

    float leading = (g_textLeading > 0) ? g_textLeading : g_textSize * 1.25f;

    // Split on \n
    std::vector<std::string> ls;
    std::string cur;
    for (char c : msg) { if(c=='\n'){ls.push_back(cur);cur.clear();}else cur+=c; }
    ls.push_back(cur);

    for (int li = 0; li < (int)ls.size(); li++) {
        float lw  = getLineWidth(ls[li]);
        float dx  = x;
        if      (g_textAlignX == RIGHT_ALIGN)  dx = x - lw;
        else if (g_textAlignX == CENTER_ALIGN) dx = x - lw * 0.5f;

        float dy = y + li * leading;

#if PROCESSING_HAS_STB_TRUETYPE
        if (g_ttf.loaded) { drawTTFStr(dx, dy, ls[li]); continue; }
#endif
        // Bitmap fallback
        int sc = std::max(1,(int)(g_textSize/8.0f));
        // Shift so baseline sits at y (bitmap font: ascent = BF_GH-2 rows)
        float ascent = (BF_GH - 2) * sc;
        drawBitmapStr(dx, dy - ascent, ls[li], sc);
    }
}

void text(const std::string& msg, float x, float y) { renderText(msg, x, y); }
void text(int   v, float x, float y) { renderText(std::to_string(v), x, y); }
void text(float v, float x, float y) {
    std::ostringstream ss; ss << v; renderText(ss.str(), x, y);
}

void textSize(float size) {
    g_textSize = size;
#if PROCESSING_HAS_STB_TRUETYPE
    if (g_ttf.loaded) bakeAtlas(size);
#endif
}

void textAlign(int alignX, int alignY) { g_textAlignX=alignX; g_textAlignY=alignY; }
void textLeading(float v) { g_textLeading = v; }
void textMode(int) {}

float textWidth(const std::string& s) { return getLineWidth(s); }

float textAscent() {
#if PROCESSING_HAS_STB_TRUETYPE
    if (g_ttf.loaded) {
        float sc = stbtt_ScaleForPixelHeight(&g_ttf.info, g_textSize);
        int asc; stbtt_GetFontVMetrics(&g_ttf.info, &asc, nullptr, nullptr);
        return asc * sc;
    }
#endif
    int sc = std::max(1,(int)(g_textSize/8.0f));
    return (BF_GH - 2) * sc;
}

float textDescent() {
#if PROCESSING_HAS_STB_TRUETYPE
    if (g_ttf.loaded) {
        float sc = stbtt_ScaleForPixelHeight(&g_ttf.info, g_textSize);
        int desc; stbtt_GetFontVMetrics(&g_ttf.info, nullptr, &desc, nullptr);
        return std::fabs(desc * sc);
    }
#endif
    int sc = std::max(1,(int)(g_textSize/8.0f));
    return 2.0f * sc;
}


// =============================================================================
// IMAGE
// =============================================================================

PImage createImage(int w,int h){return PImage(w,h);}

PImage* loadImage(const std::string& path){
    std::cerr<<"loadImage: enable stb_image in Processing.cpp: "<<path<<"\n";
    return nullptr;
}

PGraphics* createGraphics(int w,int h){return new PGraphics(w,h);}

static void drawImageRect(PImage& img,float x,float y,float w,float h){
    if(img.dirty)img.uploadTexture();
    if(img.texID==0)return;
    glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,img.texID);
    glColor4f(doTint?tintR:1,doTint?tintG:1,doTint?tintB:1,doTint?tintA:1);
    glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex2f(x,y);
    glTexCoord2f(1,0);glVertex2f(x+w,y);
    glTexCoord2f(1,1);glVertex2f(x+w,y+h);
    glTexCoord2f(0,1);glVertex2f(x,y+h);
    glEnd();
    glBindTexture(GL_TEXTURE_2D,0);glDisable(GL_TEXTURE_2D);
}
void image(PImage& img,float x,float y)           {drawImageRect(img,x,y,(float)img.width,(float)img.height);}
void image(PImage& img,float x,float y,float w,float h){drawImageRect(img,x,y,w,h);}
void imageMode(int m){currentImageMode=m;}
void tint(float gray,float a){color c=makeColor(gray,a);unsigned int v=c.value;tintR=(v>>16&0xFF)/255.f;tintG=(v>>8&0xFF)/255.f;tintB=(v&0xFF)/255.f;tintA=(v>>24&0xFF)/255.f;doTint=true;}
void tint(float r,float g,float b,float a){color c=makeColor(r,g,b,a);unsigned int v=c.value;tintR=(v>>16&0xFF)/255.f;tintG=(v>>8&0xFF)/255.f;tintB=(v&0xFF)/255.f;tintA=(v>>24&0xFF)/255.f;doTint=true;}
void noTint(){doTint=false;}

void filter(int mode){filter(mode,0.5f);}
void filter(int mode,float){
    std::vector<unsigned char> buf(winWidth*winHeight*4);
    glReadPixels(0,0,winWidth,winHeight,GL_RGBA,GL_UNSIGNED_BYTE,buf.data());
    for(int i=0;i<winWidth*winHeight;i++){
        int r=buf[i*4],g=buf[i*4+1],b=buf[i*4+2];
        if(mode==GRAY){int gr=(r+g+b)/3;buf[i*4]=buf[i*4+1]=buf[i*4+2]=gr;}
        else if(mode==INVERT){buf[i*4]=255-r;buf[i*4+1]=255-g;buf[i*4+2]=255-b;}
        else if(mode==THRESHOLD){int gr=(r+g+b)/3;int t=gr>127?255:0;buf[i*4]=buf[i*4+1]=buf[i*4+2]=t;}
    }
    glDrawPixels(winWidth,winHeight,GL_RGBA,GL_UNSIGNED_BYTE,buf.data());
}
void loadPixels(){
    pixels.resize(winWidth*winHeight);
    std::vector<unsigned char> buf(winWidth*winHeight*4);
    glReadPixels(0,0,winWidth,winHeight,GL_RGBA,GL_UNSIGNED_BYTE,buf.data());
    for(int i=0;i<winWidth*winHeight;i++)
        pixels[i]=(buf[i*4+3]<<24)|(buf[i*4]<<16)|(buf[i*4+1]<<8)|buf[i*4+2];
}
void updatePixels(){
    std::vector<unsigned char> buf(winWidth*winHeight*4);
    for(int i=0;i<winWidth*winHeight;i++){
        buf[i*4]=(pixels[i]>>16)&0xFF;buf[i*4+1]=(pixels[i]>>8)&0xFF;
        buf[i*4+2]=pixels[i]&0xFF;buf[i*4+3]=(pixels[i]>>24)&0xFF;
    }
    glDrawPixels(winWidth,winHeight,GL_RGBA,GL_UNSIGNED_BYTE,buf.data());
}
color get(int x,int y){
    unsigned char p[4];
    glReadPixels(x,winHeight-1-y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    return colorVal(p[0],p[1],p[2],p[3]);
}
void set(int x,int y,color c){
    unsigned int cv=c.value;unsigned char p[]={(unsigned char)(cv>>16&0xFF),(unsigned char)(cv>>8&0xFF),(unsigned char)(cv&0xFF),(unsigned char)(cv>>24&0xFF)};
    glWindowPos2i(x,winHeight-1-y);
    glDrawPixels(1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
}

// =============================================================================
// BLEND / CLIP
// =============================================================================

void blendMode(int mode){
    glEnable(GL_BLEND);
    switch(mode){
        case ADD:      glBlendFunc(GL_SRC_ALPHA,GL_ONE);break;
        case MULTIPLY: glBlendFunc(GL_DST_COLOR,GL_ZERO);break;
        case SCREEN:   glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_COLOR);break;
        case SUBTRACT: glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);glBlendFunc(GL_SRC_ALPHA,GL_ONE);break;
        default:       glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);break;
    }
}
void clip(float x,float y,float w,float h){glEnable(GL_SCISSOR_TEST);glScissor((int)x,winHeight-(int)(y+h),(int)w,(int)h);}
void noClip(){glDisable(GL_SCISSOR_TEST);}

// =============================================================================
// SAVE
// =============================================================================

void saveFrame(const std::string& fn){
    std::cout<<"saveFrame: "<<fn<<" (enable stb_image_write in Processing.cpp)\n";
}
void save(const std::string& fn){saveFrame(fn);}

// =============================================================================
// GLFW CALLBACKS
// =============================================================================

static bool mouseWasPressed=false;

static void cursor_pos_cb(GLFWwindow*,double x,double y){
    pmouseX=mouseX;pmouseY=mouseY;
    mouseX=(float)x;mouseY=(float)y;
    if(isMousePressed){if(_onMouseDragged)_onMouseDragged();}
    else             {if(_onMouseMoved)  _onMouseMoved();}
}
static void mouse_btn_cb(GLFWwindow*,int btn,int action,int){
    if(action==GLFW_PRESS){
        isMousePressed=true;mouseButton=(btn==GLFW_MOUSE_BUTTON_LEFT)?LEFT:(btn==GLFW_MOUSE_BUTTON_RIGHT)?RIGHT:CENTER;
        if(_onMousePressed)_onMousePressed();
        mouseWasPressed=true;
    } else if(action==GLFW_RELEASE){
        isMousePressed=false;
        if(_onMouseReleased)_onMouseReleased();
        if(mouseWasPressed&&_onMouseClicked)_onMouseClicked();
        mouseWasPressed=false;mouseButton=-1;
    }
}
static void scroll_cb(GLFWwindow*,double,double yoffset){
    if(_onMouseWheel)_onMouseWheel((int)yoffset);
}
static char g_lastChar = 0;  // set by char_cb, consumed by key_cb

static void char_cb(GLFWwindow*, unsigned int codepoint) {
    // GLFW char callback delivers the correct Unicode character
    // with shift, caps lock, and keyboard layout all applied.
    // We store it so key_cb can pick it up, and also fire keyTyped.
    if (codepoint < 128) {
        key = (char)codepoint;
        g_lastChar = key;
    }
    if (_onKeyTyped) _onKeyTyped();
}

static void key_cb(GLFWwindow* w, int k, int /*scancode*/, int action, int /*mods*/) {
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        isKeyPressed = true;
        keyCode = k;

        // For printable keys, char_cb fires right after key_cb and will set
        // key correctly (with shift/caps applied). For REPEAT events char_cb
        // also fires. For non-printable keys we set key=CODED.
        // Reset g_lastChar so we can detect whether char_cb fired.
        if (k == GLFW_KEY_ENTER || k == GLFW_KEY_KP_ENTER ||
            k == GLFW_KEY_BACKSPACE || k == GLFW_KEY_DELETE ||
            k == GLFW_KEY_TAB || k == GLFW_KEY_ESCAPE ||
            k == GLFW_KEY_UP || k == GLFW_KEY_DOWN ||
            k == GLFW_KEY_LEFT || k == GLFW_KEY_RIGHT ||
            k == GLFW_KEY_HOME || k == GLFW_KEY_END ||
            k == GLFW_KEY_PAGE_UP || k == GLFW_KEY_PAGE_DOWN ||
            k == GLFW_KEY_F1 || k == GLFW_KEY_F2 || k == GLFW_KEY_F3 ||
            k == GLFW_KEY_F4 || k == GLFW_KEY_F5 || k == GLFW_KEY_F6 ||
            k == GLFW_KEY_F7 || k == GLFW_KEY_F8 || k == GLFW_KEY_F9 ||
            k == GLFW_KEY_F10|| k == GLFW_KEY_F11|| k == GLFW_KEY_F12||
            k == GLFW_KEY_LEFT_SHIFT || k == GLFW_KEY_RIGHT_SHIFT ||
            k == GLFW_KEY_LEFT_CONTROL || k == GLFW_KEY_RIGHT_CONTROL ||
            k == GLFW_KEY_LEFT_ALT || k == GLFW_KEY_RIGHT_ALT ||
            k == GLFW_KEY_LEFT_SUPER || k == GLFW_KEY_RIGHT_SUPER ||
            k == GLFW_KEY_CAPS_LOCK || k == GLFW_KEY_NUM_LOCK) {
            key = (char)CODED;
        }
        // For printable keys key will be set correctly by char_cb which fires next.

        // ESC is handled by the sketch — do not force-close here
        if (_onKeyPressed) _onKeyPressed();

    } else if (action == GLFW_RELEASE) {
        isKeyPressed = false;
        if (_onKeyReleased) _onKeyReleased();
    }
}
static void focus_cb(GLFWwindow*,int f){focused=(f==GLFW_TRUE);}
static void winpos_cb(GLFWwindow*,int,int){if(_onWindowMoved)_onWindowMoved();}
static void winsize_cb(GLFWwindow*,int lw,int lh){
    if(!lw||!lh)return;
    winWidth=lw; winHeight=lh;
    setProjection(lw,lh);
    if(_onWindowResized)_onWindowResized();
}
static void fbsize_cb(GLFWwindow*,int fw,int fh){
    if(!fw||!fh)return;
    pixelWidth=fw;pixelHeight=fh;
}

// =============================================================================
// RUN
// =============================================================================

static bool tryLoadTTF(const std::string& path, float size); // forward decl

void run(){
    // Make stdout fully unbuffered so print()/println() appear immediately
    // in the IDE console when running via pipe capture
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::srand((unsigned)std::time(nullptr));
    buildNoisePerm(0);

    if(!glfwInit()){std::cerr<<"GLFW init failed\n";return;}
    GLFWmonitor* mon=glfwGetPrimaryMonitor();
    if(mon){const GLFWvidmode* vm=glfwGetVideoMode(mon);displayWidth=vm->width;displayHeight=vm->height;}

    // Reset all style state to defaults BEFORE setup()
    doFill=true;doStroke=true;
    fillR=1;fillG=1;fillB=1;fillA=1;
    strokeR=0;strokeG=0;strokeB=0;strokeA=1;strokeW=1;
    colorModeVal=RGB;
    colorMaxH=255;colorMaxS=255;colorMaxB=255;colorMaxA=255;
    currentRectMode=CORNER;currentEllipseMode=CENTER_MODE;currentImageMode=CORNER;
    doTint=false;tintR=1;tintG=1;tintB=1;tintA=1;
    lightsEnabled=false;lightIndex=0;
    looping=true;

    settings();

    glfwWindowHint(GLFW_RESIZABLE,isResizable?GLFW_TRUE:GLFW_FALSE);
    glfwWindowHint(GLFW_SAMPLES,4);
    glfwWindowHint(GLFW_STENCIL_BITS,8);  // needed for concave shape fill
    gWindow=glfwCreateWindow(winWidth,winHeight,"ProcessingCPP",nullptr,nullptr);
    if(!gWindow){std::cerr<<"Window creation failed\n";glfwTerminate();return;}
    glfwMakeContextCurrent(gWindow);
    if(glewInit()!=GLEW_OK){std::cerr<<"GLEW init failed\n";glfwDestroyWindow(gWindow);glfwTerminate();return;}

    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_NORMALIZE);
    smooth();
    setProjection(winWidth,winHeight);
    {int fw,fh;glfwGetFramebufferSize(gWindow,&fw,&fh);pixelWidth=fw;pixelHeight=fh;}

    glfwSetCursorPosCallback(gWindow,   cursor_pos_cb);
    glfwSetMouseButtonCallback(gWindow, mouse_btn_cb);
    glfwSetScrollCallback(gWindow,      scroll_cb);
    glfwSetKeyCallback(gWindow,         key_cb);
    glfwSetCharCallback(gWindow,        char_cb);
    glfwSetFramebufferSizeCallback(gWindow,fbsize_cb);
    glfwSetWindowSizeCallback(gWindow,  winsize_cb);
    glfwSetWindowFocusCallback(gWindow, focus_cb);
    glfwSetWindowPosCallback(gWindow,   winpos_cb);
    focused=(glfwGetWindowAttrib(gWindow,GLFW_FOCUSED)==GLFW_TRUE);

    // Auto-load default.ttf from project root as the default font
    // (matches Processing Java's "a generic sans-serif font will be used")
    // Try to load default.ttf from several common locations
    if (!tryLoadTTF("default.ttf",      g_textSize) &&
        !tryLoadTTF("src/default.ttf",  g_textSize) &&
        !tryLoadTTF("fonts/default.ttf",g_textSize)) {
        std::cerr << "[font] default.ttf not found — using bitmap fallback\n";
    }

    setup();

    // Auto-wire Java-style event functions if the sketch defines them.
    // Uses weak symbols — if the function is not defined in the sketch,
    // the weak stub resolves to nullptr and the check safely skips it.
    if((void*)::Processing::keyPressed != nullptr)   _onKeyPressed   = ::Processing::keyPressed;
    if((void*)::Processing::keyReleased  != nullptr) _onKeyReleased  = ::Processing::keyReleased;
    if((void*)::Processing::keyTyped     != nullptr) _onKeyTyped     = ::Processing::keyTyped;
    if((void*)::Processing::mousePressed != nullptr) _onMousePressed = ::Processing::mousePressed;
    if((void*)::Processing::mouseReleased!= nullptr) _onMouseReleased= ::Processing::mouseReleased;
    if((void*)::Processing::mouseClicked != nullptr) _onMouseClicked = ::Processing::mouseClicked;
    if((void*)::Processing::mouseMoved   != nullptr) _onMouseMoved   = ::Processing::mouseMoved;
    if((void*)::Processing::mouseDragged != nullptr) _onMouseDragged = ::Processing::mouseDragged;
    if((void*)::Processing::mouseWheel   != nullptr) _onMouseWheel   = ::Processing::mouseWheel;
    if((void*)::Processing::windowMoved  != nullptr) _onWindowMoved  = ::Processing::windowMoved;
    if((void*)::Processing::windowResized!= nullptr) _onWindowResized= ::Processing::windowResized;

    redrawOnce=true;
    auto last=std::chrono::steady_clock::now();
    while(!glfwWindowShouldClose(gWindow)){
        pmouseX=mouseX;pmouseY=mouseY;
        if(looping||redrawOnce){
            redrawOnce=false;

            if(defaultP3D){
                // Set up viewport
                {int fw=winWidth,fh=winHeight;if(gWindow)glfwGetFramebufferSize(gWindow,&fw,&fh);glViewport(0,0,fw,fh);}
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LESS);
                glDisable(GL_CULL_FACE);
                glFrontFace(GL_CW);
                glEnable(GL_NORMALIZE);
                glClear(GL_DEPTH_BUFFER_BIT);
                // Auto-apply the default Processing camera BEFORE draw() —
                // matches Java Processing behaviour exactly. The sketch can
                // override by calling camera() / perspective() itself.
                // This means lights() called anywhere in draw() will always
                // see the camera matrix and lock correctly in eye space.
                applyDefaultCamera();
            } else {
                // 2D mode: disable depth test so all shapes draw in order
                glDisable(GL_DEPTH_TEST);
                glDisable(GL_LIGHTING);
                setProjection(winWidth,winHeight);
                glClear(GL_DEPTH_BUFFER_BIT);
            }

            draw();++frameCount;

            glfwSwapBuffers(gWindow);
        }
        glfwPollEvents();
        auto now=std::chrono::steady_clock::now();
        double elapsed=std::chrono::duration<double>(now-last).count();
        if(elapsed>0) measuredFrameRate=measuredFrameRate*0.9f+(float)(1.0/elapsed)*0.1f;
        double sl=targetFrameTime-elapsed;
        if(sl>0)std::this_thread::sleep_for(std::chrono::duration<double>(sl));
        last=std::chrono::steady_clock::now();
    }
    glfwDestroyWindow(gWindow);gWindow=nullptr;glfwTerminate();
}

// =============================================================================
// JSON IMPLEMENTATION
// =============================================================================

static void skipWS(const std::string& s, size_t& i){
    while(i<s.size()&&(s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r'))i++;
}
static std::string parseString(const std::string& s, size_t& i){
    i++;
    std::string r;
    while(i<s.size()&&s[i]!='"'){
        if(s[i]=='\\'&&i+1<s.size()){i++;
            switch(s[i]){case '"':r+='"';break;case '\\':r+='\\';break;case '/':r+='/';break;
                         case 'n':r+='\n';break;case 't':r+='\t';break;case 'r':r+='\r';break;default:r+=s[i];break;}
        } else r+=s[i];
        i++;
    }
    if(i<s.size())i++;
    return r;
}
static JSONValue parseJSONValue(const std::string& s, size_t& i);
static JSONObject parseJSONObj(const std::string& s, size_t& i){
    JSONObject obj; i++;
    skipWS(s,i);
    while(i<s.size()&&s[i]!='}'){
        skipWS(s,i); if(s[i]!='"')break;
        std::string key=parseString(s,i);
        skipWS(s,i); if(i<s.size()&&s[i]==':')i++;
        skipWS(s,i);
        obj[key]=parseJSONValue(s,i);
        skipWS(s,i); if(i<s.size()&&s[i]==',')i++;
        skipWS(s,i);
    }
    if(i<s.size())i++;
    return obj;
}
static JSONArray parseJSONArr(const std::string& s, size_t& i){
    JSONArray arr; i++;
    skipWS(s,i);
    while(i<s.size()&&s[i]!=']'){
        skipWS(s,i);
        arr.push_back(parseJSONValue(s,i));
        skipWS(s,i); if(i<s.size()&&s[i]==',')i++;
        skipWS(s,i);
    }
    if(i<s.size())i++;
    return arr;
}
static JSONValue parseJSONValue(const std::string& s, size_t& i){
    skipWS(s,i);
    if(i>=s.size()) return JSONValue();
    if(s[i]=='"')  return JSONValue(parseString(s,i));
    if(s[i]=='{')  return JSONValue(parseJSONObj(s,i));
    if(s[i]=='[')  return JSONValue(parseJSONArr(s,i));
    if(s.substr(i,4)=="null") { i+=4; return JSONValue(); }
    if(s.substr(i,4)=="true") { i+=4; return JSONValue(true); }
    if(s.substr(i,5)=="false"){ i+=5; return JSONValue(false); }
    size_t start=i;
    bool isFloat=false;
    if(i<s.size()&&s[i]=='-')i++;
    while(i<s.size()&&(std::isdigit(s[i])||s[i]=='.'||s[i]=='e'||s[i]=='E'||s[i]=='+'||s[i]=='-')){
        if(s[i]=='.'||s[i]=='e'||s[i]=='E')isFloat=true; i++;
    }
    std::string num=s.substr(start,i-start);
    if(isFloat) return JSONValue(std::stod(num));
    return JSONValue(std::stoi(num));
}

JSONValue parseJSON(const std::string& src){ size_t i=0; return parseJSONValue(src,i); }

std::string toJSONString(const JSONValue& v, int indent){
    std::string pad(indent*2,' ');
    std::string pad2((indent+1)*2,' ');
    switch(v.type){
        case JSONValue::NULL_T:   return "null";
        case JSONValue::BOOL_T:   return v.b?"true":"false";
        case JSONValue::INT_T:    return std::to_string((int)v.n);
        case JSONValue::FLOAT_T:  { std::ostringstream ss; ss<<v.n; return ss.str(); }
        case JSONValue::STRING_T: return "\""+v.s+"\"";
        case JSONValue::ARRAY_T: {
            if(v.arr->empty())return "[]";
            std::string r="[\n";
            for(size_t i=0;i<v.arr->size();i++){
                r+=pad2+toJSONString((*v.arr)[i],indent+1);
                if(i+1<v.arr->size())r+=",";
                r+="\n";
            }
            return r+pad+"]";
        }
        case JSONValue::OBJECT_T: {
            if(v.obj->empty())return "{}";
            std::string r="{\n";
            size_t n=0;
            for(auto& p:*v.obj){
                r+=pad2+"\""+p.first+"\": "+toJSONString(p.second,indent+1);
                if(++n<v.obj->size())r+=",";
                r+="\n";
            }
            return r+pad+"}";
        }
    }
    return "null";
}

static std::string readFileString(const std::string& path){
    std::ifstream f(path); if(!f)return "";
    return std::string((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
}

JSONValue loadJSONObject(const std::string& path){ return parseJSON(readFileString(path)); }
JSONValue loadJSONArray(const std::string& path) { return parseJSON(readFileString(path)); }
bool saveJSONObject(const std::string& path,const JSONValue& v,int indent){
    std::ofstream f(path); if(!f)return false; f<<toJSONString(v,indent); return true;
}
bool saveJSONArray(const std::string& path,const JSONValue& v,int indent){
    return saveJSONObject(path,v,indent);
}

// =============================================================================
// XML IMPLEMENTATION
// =============================================================================

static void xmlSkipWS(const std::string& s,size_t& i){ while(i<s.size()&&std::isspace(s[i]))i++; }

static XML parseXMLNode(const std::string& s, size_t& i){
    XML node;
    xmlSkipWS(s,i);
    if(i>=s.size()||s[i]!='<')return node;
    i++;
    if(i<s.size()&&s[i]=='?'){ while(i<s.size()&&!(s[i]=='>'||s.substr(i,2)=="?>"))i++; if(i<s.size())i++; return node; }
    if(i<s.size()&&s[i]=='!'){ while(i<s.size()&&s[i]!='>')i++; i++; return node; }
    while(i<s.size()&&!std::isspace(s[i])&&s[i]!='>'&&s[i]!='/')node.name+=s[i++];
    xmlSkipWS(s,i);
    while(i<s.size()&&s[i]!='>'&&s[i]!='/'){
        std::string key; while(i<s.size()&&!std::isspace(s[i])&&s[i]!='='&&s[i]!='>'&&s[i]!='/')key+=s[i++];
        xmlSkipWS(s,i);
        if(i<s.size()&&s[i]=='='){i++;xmlSkipWS(s,i);
            char q=s[i++]; std::string val;
            while(i<s.size()&&s[i]!=q)val+=s[i++]; if(i<s.size())i++;
            node.attributes[key]=val;
        }
        xmlSkipWS(s,i);
    }
    if(i<s.size()&&s[i]=='/'){i++;if(i<s.size())i++;return node;}
    if(i<s.size())i++;
    while(i<s.size()){
        xmlSkipWS(s,i);
        if(i+1<s.size()&&s[i]=='<'&&s[i+1]=='/'){
            i++; while(i<s.size()&&s[i]!='>')i++; if(i<s.size())i++; break;
        }
        if(i<s.size()&&s[i]=='<'){ XML child=parseXMLNode(s,i); if(!child.name.empty())node.children.push_back(child); }
        else { std::string text; while(i<s.size()&&s[i]!='<')text+=s[i++]; auto t=text;t.erase(0,t.find_first_not_of(" \t\n\r"));t.erase(t.find_last_not_of(" \t\n\r")+1);if(!t.empty())node.content+=t; }
    }
    return node;
}

XML parseXML(const std::string& src){ size_t i=0; return parseXMLNode(src,i); }
XML loadXML(const std::string& path){ return parseXML(readFileString(path)); }

std::string XML::toString(int indent) const {
    std::string pad(indent*2,' ');
    std::string r=pad+"<"+name;
    for(auto& a:attributes)r+=" "+a.first+"=\""+a.second+"\"";
    if(children.empty()&&content.empty()){r+="/>\n";return r;}
    r+=">";
    if(!content.empty())r+=content;
    if(!children.empty()){r+="\n";for(auto& c:children)r+=c.toString(indent+1);r+=pad;}
    r+="</"+name+">\n";
    return r;
}
bool saveXML(const std::string& path,const XML& x){ std::ofstream f(path);if(!f)return false;f<<x.toString();return true; }

// =============================================================================
// TABLE IMPLEMENTATION
// =============================================================================

Table* loadTable(const std::string& path,const std::string& options){
    Table* t=new Table();
    bool hasHeader=options.find("header")!=std::string::npos;
    char delim=path.find(".tsv")!=std::string::npos?'\t':',';
    auto lines=loadStrings(path);
    if(lines.empty())return t;
    int start=0;
    if(hasHeader){
        auto cols=split(lines[0],delim);
        for(auto& c:cols)t->addColumn(c);
        start=1;
    }
    for(int i=start;i<(int)lines.size();i++){
        auto& row=t->addRow();
        auto cells=split(lines[i],delim);
        for(int j=0;j<(int)cells.size()&&j<(int)row.size();j++)row[j]=cells[j];
    }
    return t;
}
bool saveTable(const std::string& path,const Table& t,const std::string& ext){
    std::ofstream f(path);if(!f)return false;
    char delim=ext=="tsv"?'\t':',';
    if(!t.columns.empty()){for(size_t i=0;i<t.columns.size();i++){if(i)f<<delim;f<<t.columns[i];}f<<"\n";}
    for(auto& row:t.rows){for(size_t i=0;i<row.size();i++){if(i)f<<delim;f<<row[i];}f<<"\n";}
    return true;
}

// =============================================================================
// PSHAPE IMPLEMENTATION
// =============================================================================

static int shapeDrawMode=CORNER;
void shapeMode(int mode){ shapeDrawMode=mode; }
PShape createShape(int kind){ return PShape(kind); }
PShape* loadShape(const std::string& path){ std::cerr<<"loadShape: SVG not implemented: "<<path<<"\n"; return new PShape(); }

static void drawPShape(const PShape& s,float x,float y,float w=-1,float h=-1){
    if(!s.visible)return;
    glPushMatrix();
    glTranslatef(x,y,0);
    if(w>0&&h>0)glScalef(w,h,1);
    for(auto& c:s.children)drawPShape(c,0,0);
    if(!s.verts.empty()){
        GLenum gm;
        switch(s.kind){
            case POINTS:gm=GL_POINTS;break;case LINES:gm=GL_LINES;break;
            case TRIANGLES:gm=GL_TRIANGLES;break;case TRIANGLE_FAN:gm=GL_TRIANGLE_FAN;break;
            case TRIANGLE_STRIP:gm=GL_TRIANGLE_STRIP;break;case QUADS:gm=GL_QUADS;break;
            case QUAD_STRIP:gm=GL_QUAD_STRIP;break;default:gm=GL_POLYGON;break;
        }
        if(s.hasFill){glColor4f(s.fillR,s.fillG,s.fillB,s.fillA);glBegin(gm);for(auto& v:s.verts)glVertex3f(v.x,v.y,v.z);glEnd();}
        if(s.hasStroke){glColor4f(s.strokeR,s.strokeG,s.strokeB,s.strokeA);glLineWidth(s.strokeW);glBegin(s.closed?GL_LINE_LOOP:GL_LINE_STRIP);for(auto& v:s.verts)glVertex3f(v.x,v.y,v.z);glEnd();}
    }
    glPopMatrix();
}
void shape(const PShape& s,float x,float y)          { drawPShape(s,x,y); }
void shape(const PShape& s,float x,float y,float w,float h){ drawPShape(s,x,y,w,h); }

// =============================================================================
// PFONT / TYPOGRAPHY
// =============================================================================

static PFont currentFont;

// Internal helper — try to load a TTF by path
static bool tryLoadTTF(const std::string& path, float size) {
#if PROCESSING_HAS_STB_TRUETYPE
    if (loadTTFFile(path)) {
        g_ttf.loaded = true;
        g_textSize   = size;
        bakeAtlas(size);
        // font load success — silent (stderr only for failures)
        return true;
    }
    std::cerr << "[font] failed to load: " << path << "\n";
#else
    std::cerr << "[font] stb_truetype not available — put stb_truetype.h in src/\n";
#endif
    return false;
}

// loadFont — loads a .ttf file; falls back to bitmap font gracefully
PFont loadFont(const std::string& filename) {
    PFont f(filename, g_textSize);
    tryLoadTTF(filename, g_textSize);
    return f;
}

// createFont — creates font from a name/path and size
PFont createFont(const std::string& name, float size, bool /*smooth*/) {
    PFont f(name, size);
    // Try as file path first, then common system paths
    std::vector<std::string> paths = {
        name,
        name + ".ttf",
        std::string("fonts/") + name + ".ttf",
        std::string("/usr/share/fonts/truetype/dejavu/") + name + ".ttf",
    };
    for (auto& p : paths) if (tryLoadTTF(p, size)) break;
    return f;
}

// textFont — switch to a previously loaded font
void textFont(const PFont& font) {
    currentFont = font;
    g_textSize  = font.size;
    tryLoadTTF(font.name, font.size);
}
void textFont(const PFont& font, float size) {
    currentFont      = font;
    currentFont.size = size;
    g_textSize       = size;
    tryLoadTTF(font.name, size);
}

// =============================================================================
// TEXTURE
// =============================================================================

static int textureModeVal=IMAGE;
static int textureWrapVal=CLAMP;
void textureMode(int mode){ textureModeVal=mode; }
void textureWrap(int mode){
    textureWrapVal=mode;
    GLint wrap=mode==REPEAT?GL_REPEAT:GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,wrap);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,wrap);
}
void texture(PImage& img){
    if(img.dirty)img.uploadTexture();
    if(img.texID){ glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,img.texID); }
}

// =============================================================================
// FILE / IO HELPERS
// =============================================================================

BufferedReader* createReader(const std::string& path){ return new BufferedReader(path); }
PrintWriter*    createWriter(const std::string& path){ return new PrintWriter(path); }

std::string selectInput(const std::string& prompt,const std::string&){
    std::string cmd="zenity --file-selection --title=\""+prompt+"\" 2>/dev/null";
    FILE* p=popen(cmd.c_str(),"r"); if(!p)return "";
    char buf[4096]=""; fgets(buf,sizeof(buf),p); pclose(p);
    std::string r(buf); if(!r.empty()&&r.back()=='\n')r.pop_back(); return r;
}
std::string selectOutput(const std::string& prompt,const std::string&){
    std::string cmd="zenity --file-selection --save --title=\""+prompt+"\" 2>/dev/null";
    FILE* p=popen(cmd.c_str(),"r"); if(!p)return "";
    char buf[4096]=""; fgets(buf,sizeof(buf),p); pclose(p);
    std::string r(buf); if(!r.empty()&&r.back()=='\n')r.pop_back(); return r;
}
std::string selectFolder(const std::string& prompt){
    std::string cmd="zenity --file-selection --directory --title=\""+prompt+"\" 2>/dev/null";
    FILE* p=popen(cmd.c_str(),"r"); if(!p)return "";
    char buf[4096]=""; fgets(buf,sizeof(buf),p); pclose(p);
    std::string r(buf); if(!r.empty()&&r.back()=='\n')r.pop_back(); return r;
}

PImage* requestImage(const std::string& path){
    PImage* img=new PImage(1,1);
    std::thread([img,path]{
        std::cerr<<"requestImage: enable stb_image for: "<<path<<"\n";
    }).detach();
    return img;
}

// =============================================================================
// PSHADER IMPLEMENTATION
// =============================================================================

static std::string readShaderFile(const std::string& path){
    std::ifstream f(path); if(!f)return "";
    return std::string((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
}

static PShader* activeShader=nullptr;

PShader* loadShader(const std::string& fragPath,const std::string& vertPath){
    std::string fSrc=readShaderFile(fragPath);
    std::string vSrc=vertPath.empty()?
        "#version 120\nvoid main(){gl_Position=ftransform();gl_TexCoord[0]=gl_MultiTexCoord0;gl_FrontColor=gl_Color;}":
        readShaderFile(vertPath);
    if(fSrc.empty()){std::cerr<<"loadShader: could not read "<<fragPath<<"\n";return nullptr;}
    PShader* s=new PShader(vSrc,fSrc);
    s->compile();
    return s;
}
void shader(PShader& s){ s.bind(); activeShader=&s; }
void resetShader(){ glUseProgram(0); activeShader=nullptr; }

// =============================================================================
// DISPLAY blend() and copy()
// =============================================================================

void blend(int sx,int sy,int sw,int sh,int dx,int dy,int dw,int dh,int mode){
    std::vector<unsigned char> src(sw*sh*4);
    glReadPixels(sx,winHeight-(sy+sh),sw,sh,GL_RGBA,GL_UNSIGNED_BYTE,src.data());
    std::vector<unsigned char> dst(dw*dh*4);
    for(int y=0;y<dh;y++) for(int x=0;x<dw;x++){
        int srcX=(int)(x*(float)sw/dw), srcY=(int)(y*(float)sh/dh);
        for(int c=0;c<4;c++) dst[(y*dw+x)*4+c]=src[(srcY*sw+srcX)*4+c];
    }
    GLenum sfact=GL_SRC_ALPHA,dfact=GL_ONE_MINUS_SRC_ALPHA;
    switch(mode){
        case ADD:     sfact=GL_SRC_ALPHA;dfact=GL_ONE;break;
        case MULTIPLY:sfact=GL_DST_COLOR;dfact=GL_ZERO;break;
        default: break;
    }
    glEnable(GL_BLEND);glBlendFunc(sfact,dfact);
    glWindowPos2i(dx,winHeight-(dy+dh));
    glDrawPixels(dw,dh,GL_RGBA,GL_UNSIGNED_BYTE,dst.data());
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}

void copy(int sx,int sy,int sw,int sh,int dx,int dy,int dw,int dh){
    blend(sx,sy,sw,sh,dx,dy,dw,dh,BLEND);
}

// =============================================================================
// getRegion
// =============================================================================

PImage getRegion(int x,int y,int w,int h){
    PImage img(w,h);
    std::vector<unsigned char> buf(w*h*4);
    glReadPixels(x,winHeight-(y+h),w,h,GL_RGBA,GL_UNSIGNED_BYTE,buf.data());
    for(int iy=0;iy<h;iy++) for(int ix=0;ix<w;ix++){
        int si=((h-1-iy)*w+ix)*4;
        int di=iy*w+ix;
        img.pixels[di]=(buf[si+3]<<24)|(buf[si]<<16)|(buf[si+1]<<8)|buf[si+2];
    }
    img.dirty=true;
    return img;
}

} // namespace Processing
