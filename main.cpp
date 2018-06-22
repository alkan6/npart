#include <iostream>
#include <vector>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/io.hpp>

enum {PRG_CUBE, PRG_COMPUTE, PRG_PARTICLE, PRG_COUNT};
enum {VAO_CUBE, VAO_COMPUTE, VAO_PARTICLE0, VAO_PARTICLE1, VAO_COUNT};
enum {VBO_CUBE, VBO_COMPUTE, VBO_PARTICLE0, VBO_PARTICLE1, VBO_COUNT};
enum {EBO_CUBE, EBO_COUNT};
enum {XFB_PARTICLE, XFB_COUNT};

static const GLchar * cubeVertShader =
        "#version 420 core\n"
        "in vec4 vertPos;"
        "uniform mat4 mvpMat;"
        "void main(){"
        "  gl_Position = mvpMat * vertPos;"
        "}";

static const GLchar * cubeFragShader =
        "#version 420 core\n"
        "out vec4 fragColor;"
        "void main(){"
        "  fragColor = vec4(0.0f,1.0f,1.0f,1.0f);"
        "}";

static const GLchar * partCompShader =
        "#version 420 core\n"
        "in vec4 partPos;"
        "void main(){"
        "  gl_Position = partPos;"
        "}";

static const GLchar * partVertShader =
        "#version 420 core\n"
        "in vec4 partPos;"
        "out vec4 pos;"
        "uniform mat4 mvpMat;"
        "void main(){"
        "  pos = partPos;"
        "  gl_Position = mvpMat * partPos;"
        "}";

static const GLchar * partGeomShader =
        "#version 420 core\n"
        "layout (points) in;"
        "layout (points, max_vertices=1) out;"
        "in vec4 pos[];"
        "out vec4 partPosNext;"
        "uniform samplerBuffer geom;"
        "void main(){"
        "  partPosNext = pos[0];"
        ""
        "  gl_Position = gl_in[0].gl_Position;"
        "  EmitVertex();"
        "  EndPrimitive();"
        "}";

static const GLchar * partFragShader =
        "#version 420 core\n"
        "out vec4 fragColor;"
        "void main(){"
        "  fragColor = vec4(1.0f,1.0f,1.0f,1.0f);"
        "}";

static const GLfloat cubeVert[][4] = {
    {-1.0f,-1.0f,-1.0f, 1.0f},
    {-1.0f,-1.0f, 1.0f, 1.0f},
    {-1.0f, 1.0f,-1.0f, 1.0f},
    {-1.0f, 1.0f, 1.0f, 1.0f},
    { 1.0f,-1.0f,-1.0f, 1.0f},
    { 1.0f,-1.0f, 1.0f, 1.0f},
    { 1.0f, 1.0f,-1.0f, 1.0f},
    { 1.0f, 1.0f, 1.0f, 1.0f}
};

static const GLushort cubeInd[] = {0,1,2,3,4,5,6,7,
                                   0,2,1,3,4,6,5,7,
                                   0,4,1,5,2,6,3,7};

typedef struct {
    GLFWwindow *wnd;
    double t;
    glm::mat4 proj;
    glm::mat4 view;
    glm::mat4 model;
    GLfloat fov;
    glm::vec4 eye;
    GLboolean autoRot;
    GLuint prg[PRG_COUNT];//program
    GLuint vao[VAO_COUNT];//vertex array
    GLuint vbo[VBO_COUNT];//vertex buffer
    GLuint ebo[EBO_COUNT];//element buffer
    GLuint xfb[XFB_COUNT];//Transform feedback
    GLuint popCount;
    GLuint frame;
} UserData;

void onGLFWError(int, const char * msg)
{
    std::cerr << "GLFW :" << msg << std::endl;
    exit(-1);
}

void GLAPIENTRY onDebug(GLenum source,
                        GLenum type,
                        GLuint,
                        GLenum severity,
                        GLsizei,
                        const GLchar* message,
                        const void*)
{
    std::cerr << std::hex
              << "type:0x" << type << ", "
              << "severity:0x" << severity << ", "
              << "source:0x" << source << ", "
              << message << std::endl;
    if(type==GL_DEBUG_TYPE_ERROR || severity!=GL_DEBUG_SEVERITY_NOTIFICATION) exit(-1);
}

void onResize(GLFWwindow *wnd, int w, int h)
{
    UserData *d = (UserData*)glfwGetWindowUserPointer(wnd);
    if(!d) exit(-1);

    float aspect = float(w)/float(h);
    glViewport(0,0,w,h);
    glScissor(0,0,w,h);

    d->proj = glm::perspective(glm::radians(d->fov), aspect, 0.01f, 100.0f);
}

void onFocus(GLFWwindow *, int)
{

}

void onKey(GLFWwindow *wnd, int key, int, int action, int)
{
    UserData *d = (UserData*)glfwGetWindowUserPointer(wnd);
    if(!d) exit(-1);

    if(action != GLFW_RELEASE) return;

    if(key==GLFW_KEY_ESCAPE){
        glfwSetWindowShouldClose(wnd,GLFW_TRUE);
    } else if(key==GLFW_KEY_R){
        d->autoRot = !d->autoRot;
    }
}

void onMouseButton(GLFWwindow *, int, int, int)
{
}

void onCursorPos(GLFWwindow *, double, double)
{
}

void onCursorEnter(GLFWwindow *, int )
{
}

void onScroll(GLFWwindow *wnd, double, double dy)
{
    UserData *d = (UserData*)glfwGetWindowUserPointer(wnd);
    if(!d) exit(-1);

    if(dy>0) { d->eye *= 1.1f;}
    else if(dy<0) { d->eye *= 0.9f;}
    d->view = glm::lookAt(glm::vec3(d->eye),
                          glm::vec3(0.0f,0.0f,0.0f),
                          glm::vec3(0.0f,1.0f,0.0f));
}

GLuint loadShaders(const std::vector<GLenum> &type,
                   const std::vector<std::string> &shader,
                   const std::vector<const GLchar *> &xfb = std::vector<const GLchar*>())
{
    if(type.empty() || shader.empty()) return 0;
    if(type.size() != shader.size()) return 0;

    GLint res;
    int infoLength;
    std::vector<GLuint> sos(type.size());
    GLuint po = glCreateProgram();

    for(size_t i=0;i<type.size();i++){
        GLuint so = glCreateShader(type[i]);
        const GLchar * p = shader[i].c_str();
        glShaderSource(so,1,&p,NULL);
        glCompileShader(so);
        glGetShaderiv(so, GL_COMPILE_STATUS, &res);
        if ( !res ){
            glGetShaderiv(so, GL_INFO_LOG_LENGTH, &infoLength);
            std::vector<char> msg(infoLength+1);
            glGetShaderInfoLog(so, infoLength, NULL, &msg[0]);
            std::cerr << "shader " << i << " " << std::string(&msg[0]);
            return 0;
        }
        glAttachShader(po, so);
        sos[i] = so;
    }

    if(!xfb.empty()){
        glTransformFeedbackVaryings(po,
                                    xfb.size(), xfb.data(),
                                    GL_INTERLEAVED_ATTRIBS);
    }

    glLinkProgram(po);
    glGetProgramiv(po, GL_LINK_STATUS, &res);
    if ( !res ){
        glGetProgramiv(po, GL_INFO_LOG_LENGTH, &infoLength);
        std::vector<char> msg(infoLength+1);
        glGetProgramInfoLog(po, infoLength, NULL, &msg[0]);
        std::cerr << std::string(&msg[0]) << std::endl;
        return 0;
    }

    for(size_t i=0;i<sos.size();i++){
        glDetachShader(po, sos[i]);
        glDeleteShader(sos[i]);
    }

    return po;
}

void initCubeRenderer(UserData *d)
{
    //load shaders
    GLuint prg = loadShaders(
    {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER},
    {cubeVertShader, cubeFragShader});
    if(!prg) exit(-1);
    d->prg[PRG_CUBE] = prg;

    GLuint vao = d->vao[VAO_CUBE];
    GLuint vbo = d->vbo[VBO_CUBE];
    GLuint ebo = d->ebo[EBO_CUBE];

    glUseProgram(prg);

    GLint vertPosLoc = glGetAttribLocation(prg,"vertPos");
    if(vertPosLoc<0) exit(-1);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,8*sizeof(glm::vec4),cubeVert,GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,24*sizeof(GLushort),cubeInd,GL_STATIC_DRAW);

    glVertexAttribPointer(vertPosLoc,4,GL_FLOAT,GL_FALSE,0,(const void*)0);
    glEnableVertexAttribArray(vertPosLoc);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER,0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    glUseProgram(0);

}

void initPartUpdater(UserData *d)
{
    GLuint prg = loadShaders(
    {GL_VERTEX_SHADER},
    {partCompShader});
    if(!prg) exit(-1);
    d->prg[PRG_COMPUTE] = prg;

    GLuint vao = d->vao[VAO_COMPUTE];
    GLuint vbo = d->vbo[VBO_COMPUTE];

    glUseProgram(prg);

    //locations
    GLint partPosLoc = glGetAttribLocation(prg,"partPos");
    if(partPosLoc<0) exit(-1);

    glBindVertexArray(vao);

    //pos
    glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glVertexAttribPointer(partPosLoc,4,GL_FLOAT,GL_FALSE,0,(const void*)0);
    glEnableVertexAttribArray(partPosLoc);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER,0);
    glUseProgram(0);
}

void initPartRenderer(UserData *d)
{
    GLuint prg = loadShaders(
    {GL_VERTEX_SHADER,GL_GEOMETRY_SHADER, GL_FRAGMENT_SHADER},
    {partVertShader, partGeomShader, partFragShader},
    {"partPosNext"});
    if(!prg) exit(-1);
    d->prg[PRG_PARTICLE] = prg;

    GLuint vao0 = d->vao[VAO_PARTICLE0];
    GLuint vao1 = d->vao[VAO_PARTICLE1];
    GLuint vbo0 = d->vbo[VBO_PARTICLE0];
    GLuint vbo1 = d->vbo[VBO_PARTICLE1];
    GLuint xfb = d->xfb[XFB_PARTICLE];

    glUseProgram(prg);

    //locations
    GLint partPosLoc = glGetAttribLocation(prg,"partPos");
    if(partPosLoc<0) exit(-1);

    glBindVertexArray(vao0);
    //initial particles
    glBindBuffer(GL_ARRAY_BUFFER,vbo0);
    glBufferData(GL_ARRAY_BUFFER,d->popCount*sizeof(glm::vec4),NULL,GL_DYNAMIC_DRAW);
    glm::vec4 * partPos = (glm::vec4*)glMapBuffer(GL_ARRAY_BUFFER,GL_WRITE_ONLY);
    for(size_t i=0;i<d->popCount;i++){
        float pa = (float)rand();
        float pb = (float)rand();
        float pr = float(rand())/float(RAND_MAX);
        *(partPos+i) = glm::vec4(pr*std::cos(pb)*std::cos(pa),
                                 pr*std::sin(pb),
                                 pr*std::cos(pb)*std::sin(pa),
                                 1.0f);
    }
    glUnmapBuffer(GL_ARRAY_BUFFER);

    glVertexAttribPointer(partPosLoc,4,GL_FLOAT,GL_FALSE,0,(const void*)0);
    glEnableVertexAttribArray(partPosLoc);

    glBindTransformFeedback(GL_TRANSFORM_FEEDBACK,xfb);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,vbo1);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,
                 d->popCount*sizeof(glm::vec4),
                 NULL,
                 GL_DYNAMIC_DRAW);
    glTransformFeedbackBufferBase(xfb,0,vbo1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER,0);


    glBindVertexArray(vao1);
//    //initial particles
    glBindBuffer(GL_ARRAY_BUFFER,vbo1);
    //glBufferData(GL_ARRAY_BUFFER,d->popCount*sizeof(glm::vec4),NULL,GL_DYNAMIC_DRAW);
    glVertexAttribPointer(partPosLoc,4,GL_FLOAT,GL_FALSE,0,(const void*)0);
    glEnableVertexAttribArray(partPosLoc);

    glBindTransformFeedback(GL_TRANSFORM_FEEDBACK,xfb);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,vbo0);
//    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,
//                 d->popCount*sizeof(glm::vec4),
//                 NULL,
//                 GL_DYNAMIC_DRAW);
    glTransformFeedbackBufferBase(xfb,0,vbo0);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER,0);

    glUseProgram(0);
}

void init(UserData *d)
{
    //USer Data
    d->frame = 0;
    d->popCount = 2;
    d->autoRot = GLFW_FALSE;
    d->fov = 55.0f;
    d->eye = glm::vec4(0.0f, 0.0f, 3.2f, 1.0f);
    d->proj = glm::mat4(1.0f);
    d->model = glm::mat4(1.0f);
    d->view = glm::lookAt(glm::vec3(d->eye),
                          glm::vec3(0.0f,0.0f,0.0f),
                          glm::vec3(0.0f,1.0f,0.0f));

    //GLFW and Window
    glfwSetErrorCallback(onGLFWError);
    if(!glfwInit()) exit(-1);

    glfwDefaultWindowHints();
    //glfwWindowHint(GLFW_SAMPLES,4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);

    GLFWmonitor *mon = glfwGetPrimaryMonitor();
    if(!mon) exit(-1);
    const GLFWvidmode *mod = glfwGetVideoMode(mon);
    if(!mod) exit(-1);
    int w = mod->width/2;
    int h = mod->height/2;

    GLFWwindow *window = glfwCreateWindow(w,h,"nPart",NULL,NULL);
    if(!window) exit(-1);
    d->wnd = window;
    glfwSetWindowUserPointer(window,d);

    glfwMakeContextCurrent(window);
    glfwSetWindowSizeCallback(window,onResize);
    glfwSetWindowFocusCallback(window,onFocus);
    glfwSetKeyCallback(window,onKey);
    glfwSetMouseButtonCallback(window,onMouseButton);
    glfwSetCursorPosCallback(window, onCursorPos);
    glfwSetScrollCallback(window,onScroll);
    glfwSetCursorEnterCallback(window,onCursorEnter);
    glfwSwapInterval(1);


    //GLEW
    GLenum ret = glewInit();
    if(ret != GLEW_OK){
        std::cerr << glewGetErrorString(ret);
        exit(-1);
    }

    onResize(window,w,h);

    //GL init
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(0.0f,0.0f,0.0f,0.0f);
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(onDebug,0);

    //generate objects
    glGenVertexArrays(VAO_COUNT,d->vao);
    glGenBuffers(VBO_COUNT,d->vbo);
    glGenBuffers(EBO_COUNT,d->ebo);
    glGenTransformFeedbacks(XFB_COUNT,d->xfb);

    initCubeRenderer(d);
    //initPartUpdater(d);
    initPartRenderer(d);

    glfwSetTime(0.0f);
    d->t = glfwGetTime();
}

void drawCube(UserData *d)
{
    GLuint prg = d->prg[PRG_CUBE];
    GLuint vao = d->vao[VAO_CUBE];

    glUseProgram(prg);

    GLint mvpMatLoc = glGetUniformLocation(prg,"mvpMat");
    GLint vertPosLoc = glGetAttribLocation(prg,"vertPos");
    if(mvpMatLoc<0 || vertPosLoc<0) exit(-1);

    glm::mat4 mvpMat = d->proj * d->view * d->model;
    glUniformMatrix4fv(mvpMatLoc,1,GL_FALSE,glm::value_ptr(mvpMat));

    glBindVertexArray(vao);
    glDrawElements(GL_LINES,24,GL_UNSIGNED_SHORT,NULL);
    glBindVertexArray(0);
    glUseProgram(0);
}

void updatePart(UserData *d)
{
    GLuint prg = d->prg[PRG_COMPUTE];
    GLuint vao = d->vao[VAO_COMPUTE];
    GLuint vbo = d->vbo[VBO_COMPUTE];

    glUseProgram(prg);

    GLint partPosLoc = glGetAttribLocation(prg,"partPos");
    if(partPosLoc<0) exit(-1);

    glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,2*sizeof(glm::vec4),NULL,GL_DYNAMIC_DRAW);
    glm::vec4 * partPos = (glm::vec4*)glMapBuffer(GL_ARRAY_BUFFER,GL_WRITE_ONLY);
    *(partPos) = glm::vec4(0,-1,0,1);
    *(partPos+1) = glm::vec4(0,1,0,1);
    glUnmapBuffer(GL_ARRAY_BUFFER);
    glBindBuffer(GL_ARRAY_BUFFER,0);

    glBindVertexArray(vao);
    glDrawArrays(GL_POINTS,0,2);
    glBindVertexArray(0);
    glUseProgram(0);
}

void drawPart(UserData *d)
{
    GLuint prg = d->prg[PRG_PARTICLE];
    GLuint vao0 = d->vao[VAO_PARTICLE0];
    GLuint vao1 = d->vao[VAO_PARTICLE1];
    GLuint vbo0 = d->vbo[VBO_PARTICLE0];
    GLuint vbo1 = d->vbo[VBO_PARTICLE1];
    GLuint xfb = d->xfb[XFB_PARTICLE];
    GLuint vao = (d->frame&0x1)? vao1 : vao0;
    GLuint vbo = (d->frame&0x1)? vbo0 : vbo1;

    glUseProgram(prg);

    GLint mvpMatLoc = glGetUniformLocation(prg,"mvpMat");
    GLint partPosLoc = glGetAttribLocation(prg,"partPos");
    if(mvpMatLoc<0 || partPosLoc<0) exit(-1);

    glm::mat4 mvpMat = d->proj * d->view * d->model;
    glUniformMatrix4fv(mvpMatLoc,1,GL_FALSE,glm::value_ptr(mvpMat));

    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,vbo);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,d->popCount*sizeof(glm::vec4),NULL,GL_DYNAMIC_DRAW);
    glBindVertexArray(vao);
    glTransformFeedbackBufferBase(xfb,0,vbo);

    glBeginTransformFeedback(GL_POINTS);
    glDrawArrays(GL_POINTS,0,d->popCount);
    glEndTransformFeedback();

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER,0);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,0);
    glUseProgram(0);

//    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,vbo);
//    glm::vec4 * partPos = (glm::vec4*)glMapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,GL_WRITE_ONLY);
//    for(size_t i=0;i<d->popCount;i++){
//        std::cout << *(partPos+i);
//    }
//    std::cout << std::endl;
//    glUnmapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER);
    //glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,0);
    //exit(0);

}

void display(UserData *d)
{
    double t = glfwGetTime();
    float dt = t - d->t;
    d->t = t;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if(d->autoRot){
        glm::mat4 rot = glm::rotate(glm::mat4(1.0f),
                                    glm::radians(5.0f*dt),
                                    glm::vec3(0.0f,1.0f,0.0f));
        d->eye = rot * d->eye;
        d->view = glm::lookAt(glm::vec3(d->eye),
                              glm::vec3(0.0f,0.0f,0.0f),
                              glm::vec3(0.0f,1.0f,0.0f));
    }
    drawCube(d);
    //updatePart(d);
    drawPart(d);
}

void finalize(UserData *d)
{
    glUseProgram(0);
    for(int i=0;i<PRG_COUNT;i++) glDeleteProgram(d->prg[i]);
    glDeleteVertexArrays(VAO_COUNT,d->vao);
    glDeleteBuffers(VBO_COUNT,d->vbo);
    glDeleteBuffers(EBO_COUNT,d->ebo);
    glDeleteTransformFeedbacks(XFB_COUNT,d->xfb);

    glfwDestroyWindow(d->wnd);
    glfwTerminate();
}

int main()
{
    UserData d;
    init(&d);
    do{
        display(&d);
        glfwSwapBuffers(d.wnd);
        //d.frame++;
        glfwPollEvents();
    }while(!glfwWindowShouldClose(d.wnd));

    finalize(&d);
    return 0;
}
