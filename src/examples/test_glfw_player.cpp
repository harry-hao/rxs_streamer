/*
 
  BASIC GLFW + GLXW WINDOW AND OPENGL SETUP 
  ------------------------------------------
  See https://gist.github.com/roxlu/6698180 for the latest version of the example.
 
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
 
#if defined(__linux) || defined(_WIN32)
#  include <GLXW/glxw.h>
#endif

#define GLFW_INCLUDE_GLCOREARB
#include <GLFW/glfw3.h>

#define ROXLU_USE_PNG
#define ROXLU_USE_OPENGL
#define ROXLU_USE_MATH
#define ROXLU_USE_FONT
#define ROXLU_IMPLEMENTATION
#include <tinylib.h>
 
void button_callback(GLFWwindow* win, int bt, int action, int mods);
void cursor_callback(GLFWwindow* win, double x, double y);
void key_callback(GLFWwindow* win, int key, int scancode, int action, int mods);
void char_callback(GLFWwindow* win, unsigned int key);
void error_callback(int err, const char* desc);
void resize_callback(GLFWwindow* window, int width, int height);

/* ---------------------- begin: player specific ------------------------ */
extern "C" {
#  include <rxs_streamer/rxs_decoder.h>
#  include <rxs_streamer/rxs_depacketizer.h>
#  include <rxs_streamer/rxs_receiver.h>
}

rxs_decoder decoder;
rxs_depacketizer depack;
rxs_receiver rec;

static int init_player();
static void on_vp8_frame(rxs_depacketizer* dep, uint8_t* buffer, uint32_t nbytes);
static void on_vp8_image(rxs_decoder* dec, vpx_image_t* img);
static void on_data(rxs_receiver* rec, uint8_t* buffer, uint32_t nbytes);

static const char* RXP_PLAYER_VS = ""
  "#version 330\n"
  "const vec2 pos[] = vec2[4](                \n"
  "  vec2(-1.0,  1.0),                        \n"
  "  vec2(-1.0, -1.0),                        \n"
  "  vec2( 1.0,  1.0),                        \n"
  "  vec2( 1.0, -1.0)                         \n"
  ");                                         \n"
  "                                           \n"
  " const vec2[] tex = vec2[4](               \n"
  "   vec2(0.0, 0.0),                         \n"
  "   vec2(0.0, 1.0),                         \n"
  "   vec2(1.0, 0.0),                         \n"
  "   vec2(1.0, 1.0)                          \n"
  ");                                         \n"
  "out vec2 v_tex;                            \n" 
  "void main() {                              \n"
  "   vec2 p = pos[gl_VertexID];              \n"
  "   gl_Position = vec4(p.x, p.y, 0.0, 1.0); \n"
  "   v_tex = tex[gl_VertexID];               \n"
  "}                                          \n"
  "";

static const char* RXP_PLAYER_FS = "" 
  "#version 330                                                      \n"
  "uniform sampler2D u_ytex;                                         \n"
  "uniform sampler2D u_utex;                                         \n"
  "uniform sampler2D u_vtex;                                         \n"
  "in vec2 v_tex;                                                    \n"
  "const vec3 R_cf = vec3(1.164383,  0.000000,  1.596027);           \n"
  "const vec3 G_cf = vec3(1.164383, -0.391762, -0.812968);           \n"
  "const vec3 B_cf = vec3(1.164383,  2.017232,  0.000000);           \n"
  "const vec3 offset = vec3(-0.0625, -0.5, -0.5);                    \n"
  "layout( location = 0 ) out vec4 fragcolor;                        \n" 
  "void main() {                                                     \n"
  "  float y = texture(u_ytex, v_tex).r;                             \n"
  "  float u = texture(u_utex, v_tex).r;                             \n"
  "  float v = texture(u_vtex, v_tex).r;                             \n"
  "  vec3 yuv = vec3(y,u,v);                                         \n"
  "  yuv += offset;                                                  \n"
  "  fragcolor = vec4(0.0, 0.0, 0.0, 1.0);                           \n"
  "  fragcolor.r = dot(yuv, R_cf);                                   \n"
  "  fragcolor.g = dot(yuv, G_cf);                                   \n"
  "  fragcolor.b = dot(yuv, B_cf);                                   \n"
  "}                                                                 \n"
  "";



static GLuint create_texture(int width, int height);               /* create yuv420p specific texture */
static int setup_opengl();                                         /* sets up all the openGL state for the player */

GLuint prog = 0;                                                   /* or program to render the yuv buffes */
GLuint vert = 0;                                                   /* vertex shader (see above) */
GLuint frag = 0;                                                   /* fragment shader (see above) */
GLuint vao = 0;                                                    /* we need to use a vao for attribute-less rendering */
GLuint tex_y = 0;                                                  /* y-channel texture to which we upload the y of the yuv420p */
GLuint tex_u = 0;                                                  /* u-channel texture to which we upload the u of the yuv420p */
GLuint tex_v = 0;                                                  /* v-channel texture to which we upload the v of the yuv420p */

/* ---------------------- end:   player specific ------------------------ */
 
int main() {
 
  glfwSetErrorCallback(error_callback);
 
  if(!glfwInit()) {
    printf("Error: cannot setup glfw.\n");
    exit(EXIT_FAILURE);
  }
 
  glfwWindowHint(GLFW_SAMPLES, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  
  GLFWwindow* win = NULL;
  int w = 1280;
  int h = 720;
 
  win = glfwCreateWindow(w, h, "GLFW", NULL, NULL);
  if(!win) {
    glfwTerminate();
    exit(EXIT_FAILURE);
  }
 
  glfwSetFramebufferSizeCallback(win, resize_callback);
  glfwSetKeyCallback(win, key_callback);
  glfwSetCharCallback(win, char_callback);
  glfwSetCursorPosCallback(win, cursor_callback);
  glfwSetMouseButtonCallback(win, button_callback);
  glfwMakeContextCurrent(win);
  glfwSwapInterval(1);
 
#if defined(__linux) || defined(_WIN32)
  if(glxwInit() != 0) {
    printf("Error: cannot initialize glxw.\n");
    exit(EXIT_FAILURE);
  }
#endif
 
  // ----------------------------------------------------------------
  // THIS IS WHERE YOU START CALLING OPENGL FUNCTIONS, NOT EARLIER!!
  // ----------------------------------------------------------------
  if (init_player() < 0) {
    printf("Error: cannot init player.\n");
    exit(1);
  }

  if (setup_opengl() < 0) {
    printf("Error: cannot setup the opengl objects for the player.\n");
    exit(EXIT_FAILURE);
  }

  while(!glfwWindowShouldClose(win)) {
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    rxs_receiver_update(&rec);

    /* drawing */
    glUseProgram(prog);
    glBindVertexArray(vao);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_y);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex_u);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, tex_v);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    /* ----------------------------------------------- */

    glfwSwapBuffers(win);
    glfwPollEvents();
  }
 
  glfwTerminate();
 
  return EXIT_SUCCESS;
}

void char_callback(GLFWwindow* win, unsigned int key) {
}
 
void key_callback(GLFWwindow* win, int key, int scancode, int action, int mods) {
  
  if(action != GLFW_PRESS) {
    return;
  }
 
  switch(key) {
    case GLFW_KEY_SPACE: {
      break;
    }
    case GLFW_KEY_P: {
      break;
    }
    case GLFW_KEY_R: {
      break;
    }
    case GLFW_KEY_S: {
      break;
    }
    case GLFW_KEY_C: {
      break;
    }
    case GLFW_KEY_ESCAPE: {
      glfwSetWindowShouldClose(win, GL_TRUE);
      break;
    }
  };
}
 
void resize_callback(GLFWwindow* window, int width, int height) {
}
 
void cursor_callback(GLFWwindow* win, double x, double y) {
}
 
void button_callback(GLFWwindow* win, int bt, int action, int mods) {
}
 
void error_callback(int err, const char* desc) {
  printf("GLFW error: %s (%d)\n", desc, err);
}

/* -------------------------------------------------------------------------------------- */

static GLuint create_texture(int width, int height) {
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  return tex;
}

static int setup_opengl() {
  
  vert = rx_create_shader(GL_VERTEX_SHADER, RXP_PLAYER_VS);
  frag = rx_create_shader(GL_FRAGMENT_SHADER, RXP_PLAYER_FS);
  prog = rx_create_program(vert, frag, true);
  glUseProgram(prog);
  glUniform1i(glGetUniformLocation(prog, "u_ytex"), 0);
  glUniform1i(glGetUniformLocation(prog, "u_utex"), 1);
  glUniform1i(glGetUniformLocation(prog, "u_vtex"), 2);

  glGenVertexArrays(1, &vao);

  return 0;
}

/*

  At this moment we only support YUYV420P video and here
  we create the 3 textures for each Y,U and V layer. Note 

 */

#if 0
static void on_video_frame(rxp_player* player, rxp_packet* pkt) {

  if (tex_y == 0) {
    /* create textures after we've decoded a frame. */
    tex_y = create_texture(pkt->img[0].width, pkt->img[0].height);
    tex_u = create_texture(pkt->img[1].width, pkt->img[1].height);
    tex_v = create_texture(pkt->img[2].width, pkt->img[2].height);
  }

  glBindTexture(GL_TEXTURE_2D, tex_y);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, pkt->img[0].stride);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pkt->img[0].width, pkt->img[0].height, GL_RED, GL_UNSIGNED_BYTE, pkt->img[0].data);

  glBindTexture(GL_TEXTURE_2D, tex_u);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, pkt->img[1].stride);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pkt->img[1].width, pkt->img[1].height, GL_RED, GL_UNSIGNED_BYTE, pkt->img[1].data);

  glBindTexture(GL_TEXTURE_2D, tex_v);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, pkt->img[2].stride);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pkt->img[2].width, pkt->img[2].height, GL_RED, GL_UNSIGNED_BYTE, pkt->img[2].data);
}  
#endif

static int init_player() {
  if (rxs_decoder_init(&decoder) < 0) {  return -1;  }
  if (rxs_depacketizer_init(&depack) < 0) { return -2; } 
  if (rxs_receiver_init(&rec, 6970) < 0) { return -3; } 

  rec.on_data = on_data;
  depack.on_frame = on_vp8_frame;
  decoder.on_image = on_vp8_image;
  return 0;
}

static void on_vp8_frame(rxs_depacketizer* dep, uint8_t* buffer, uint32_t nbytes) {
  rxs_decoder_decode(&decoder, buffer, nbytes);
}

static void on_data(rxs_receiver* rec, uint8_t* buffer, uint32_t nbytes) {
  rxs_depacketizer_unwrap(&depack, buffer, (int64_t)nbytes);
}

static void on_vp8_image(rxs_decoder* dec, vpx_image_t* img) {

  if (!img) { return ; } 

  if (tex_y == 0) {
    printf("- create textures.\n");

    /* create textures after we've decoded a frame. */
    tex_y = create_texture(img->d_w, img->d_h);
    tex_u = create_texture(img->d_w * 0.5, img->d_h * 0.5);
    tex_v = create_texture(img->d_w * 0.5, img->d_h * 0.5);
  }

  glBindTexture(GL_TEXTURE_2D, tex_y);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, img->stride[0]);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, img->d_w, img->d_h, GL_RED, GL_UNSIGNED_BYTE, img->planes[0]);

  int d_w = img->d_w * 0.5;
  int d_h = img->d_h * 0.5;

  glBindTexture(GL_TEXTURE_2D, tex_u);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, img->stride[1]);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, d_w, d_h, GL_RED, GL_UNSIGNED_BYTE, img->planes[1]);

  glBindTexture(GL_TEXTURE_2D, tex_v);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, img->stride[2]);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, d_w, d_h, GL_RED, GL_UNSIGNED_BYTE, img->planes[2]);
}