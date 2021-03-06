#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <lodepng.h>

#include <iostream>
#include <sstream>
#include <string>
#include <memory>
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <exception>
#include <vector>
#include <fstream>

using namespace std;
using namespace glm;

auto load_file(const string &fname) {
    ifstream file(fname);
    string line;
    vector<string> rv;
    while (getline(file, line)) {
        line += "\n";
        rv.push_back(line);
    }
    return rv;
}

auto compile_shader(GLenum type, const vector<string> &file) {
    GLuint rv = glCreateShader(type);
    if (rv == 0) {
        throw runtime_error("Failed to create shader!");
    }

    vector<const GLchar *> code;
    code.reserve(file.size());
    transform(begin(file), end(file), back_inserter(code), [](const auto &line) { return line.data(); });

    glShaderSource(rv, code.size(), &code[0], nullptr);

    glCompileShader(rv);

    GLint result;
    glGetShaderiv(rv, GL_COMPILE_STATUS, &result);
    if (result == GL_FALSE) {
        cerr << "Shader compilation failed!" << endl;

        GLint logLen;
        glGetShaderiv(rv, GL_INFO_LOG_LENGTH, &logLen);

        if (logLen > 0) {
            auto log = make_unique<GLchar[]>(logLen);
            glGetShaderInfoLog(rv, logLen, nullptr, log.get());

            cerr << "Shader compilation log:\n" << log.get() << endl;
        }
    }

    return rv;
}

auto link_program(GLuint vertex_shader, GLuint frag_shader) {
    GLuint rv = glCreateProgram();
    if (rv == 0) {
        throw runtime_error("Failed to create shader program!");
    }

    glAttachShader(rv, vertex_shader);
    glAttachShader(rv, frag_shader);
    glLinkProgram(rv);

    GLint result;
    glGetProgramiv(rv, GL_LINK_STATUS, &result);
    if (result == GL_FALSE) {
        cerr << "Shader link failed!" << endl;

        GLint logLen;
        glGetProgramiv(rv, GL_INFO_LOG_LENGTH, &logLen);

        if (logLen > 0) {
            auto log = make_unique<GLchar[]>(logLen);
            glGetProgramInfoLog(rv, logLen, nullptr, log.get());

            cerr << "Shader compilation log:\n" << log.get() << endl;
        }
    }

    return rv;
}

struct VAO {
    GLuint handle = 0;
    GLuint vbo = 0;
    int num_tris = 0;
};

VAO vao_from_obj(const string &fname, GLint posAttrib, GLint uvAttrib, GLint normAttrib) {
    ifstream file(fname);

    vector<vec3> pos;
    vector<vec2> uv;
    vector<vec3> norm;
    vector<GLfloat> data;
    int num_tris = 0;

    string line;
    string word;
    while (getline(file, line)) {
        istringstream iss(line);
        iss >> word;
        if (word == "v") {
            vec3 v;
            iss >> v.x >> v.y >> v.z;
            pos.push_back(v);
        } else if (word == "vt") {
            vec2 v;
            iss >> v.x >> v.y;
            uv.push_back(v);
        } else if (word == "vn") {
            vec3 v;
            iss >> v.x >> v.y >> v.z;
            norm.push_back(v);
        } else if (word == "f") {
            string fs[3];
            iss >> fs[0] >> fs[1] >> fs[2];
            for (auto &f : fs) {
                replace(begin(f), end(f), '/', ' ');
                istringstream fiss(f);
                int ipos;
                int iuv;
                int inorm;
                fiss >> ipos >> iuv >> inorm;
                --ipos;
                --iuv;
                --inorm;
                GLfloat vals[] = {
                        pos[ipos].x,
                        pos[ipos].y,
                        pos[ipos].z,
                        uv[iuv].x,
                        1.f - uv[iuv].y,
                        norm[inorm].x,
                        norm[inorm].y,
                        norm[inorm].z,
                };
                data.insert(end(data), begin(vals), end(vals));
                ++num_tris;
            }
        } else if (word[0] == '#') {
        } else {
            clog << "Warning: Unknown OBJ directive \"" << word << "\"" << endl;
        }
    }

    VAO vao;
    glGenBuffers(1, &vao.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vao.vbo);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(GLfloat), &data[0], GL_STATIC_DRAW);

    glGenVertexArrays(1, &vao.handle);
    vao.num_tris = num_tris;

    glBindVertexArray(vao.handle);

    auto stride = sizeof(GLfloat) * (3 + 2 + 3);
    glEnableVertexAttribArray(posAttrib);
    glEnableVertexAttribArray(uvAttrib);
    glEnableVertexAttribArray(normAttrib);
    glVertexAttribPointer(posAttrib, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const GLvoid *>(0));
    glVertexAttribPointer(uvAttrib, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const GLvoid *>(sizeof(GLfloat) * 3));
    glVertexAttribPointer(normAttrib, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const GLvoid *>(sizeof(GLfloat) * (3 + 2)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    clog << "Created vao " << vao.handle << " with " << vao.num_tris << " tris." << endl;
    return vao;
}

struct Texture {
    GLuint handle = 0;
    int width = 0;
    int height = 0;
};

Texture load_texture(const string &fname) {
    std::vector<unsigned char> image;
    unsigned width, height;
    unsigned error = lodepng::decode(image, width, height, fname);

    Texture rv;

    if (error != 0) {
        clog << "Error: Unable to load texture \"" << fname << "\"" << endl;
        return rv;
    }

    rv.width = width;
    rv.height = height;

    glGenTextures(1, &rv.handle);
    glBindTexture(GL_TEXTURE_2D, rv.handle);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, &image[0]);

    glBindTexture(GL_TEXTURE_2D, 0);

    return rv;
}

struct Texture3D {
    GLuint handle = 0;
    int width = 0;
    int height = 0;
    int depth = 0;
};

using DitherArr = vector<vector<double>>;

Texture3D gen_dithermap(int width, int height, const vector<DitherArr> &arrs) {
    std::vector<unsigned char> image;
    int depth = arrs.size();
    image.resize(width * height * depth);

    for (int d = 0; d < depth; ++d) {
        for (int r = 0; r < height; ++r) {
            for (int c = 0; c < width; ++c) {
                int i = d * width * height + r * width + c;
                auto& dm = arrs[depth-d-1];
                auto& row = dm[r % dm.size()];
                auto& pix = row[c % row.size()];
                image[i] = clamp(int(pix * 255), 0, 255);
            }
        }
    }

    Texture3D rv;
    rv.width = width;
    rv.height = height;
    rv.depth = arrs.size();

    glGenTextures(1, &rv.handle);
    glBindTexture(GL_TEXTURE_3D, rv.handle);

    glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RED, width, height, depth, 0, GL_RED, GL_UNSIGNED_BYTE, &image[0]);

    glBindTexture(GL_TEXTURE_3D, 0);

    return rv;
}

void error_cb(int error, const char *description) {
    ostringstream oss;
    oss << "ERROR " << error << ": " << description << endl;
    throw runtime_error(oss.str());
}

int main() try {
    glfwSetErrorCallback(error_cb);

    if (!glfwInit()) {
        throw runtime_error("Failed to init GLFW!");
    }

    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    int screenWidth = 800;
    int screenHeight = 600;
    GLFWwindow *window = glfwCreateWindow(screenWidth, screenHeight, "Shader Sandy", nullptr, nullptr);
    if (!window) {
        throw runtime_error("Failed to open window!");
    }

    glfwMakeContextCurrent(window);

    if (!gladLoadGL()) {
        throw runtime_error("Failed to load GL!");
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearDepth(1.f);

    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, load_file("data/vertex.glsl"));
    GLuint frag_shader = compile_shader(GL_FRAGMENT_SHADER, load_file("data/frag.glsl"));
    GLuint shader = link_program(
            vertex_shader,
            frag_shader
    );
    glDeleteShader(vertex_shader);
    glDeleteShader(frag_shader);
    glUseProgram(shader);

    glUniform1f(glGetUniformLocation(shader, "ScreenWidth"), screenWidth);
    glUniform1f(glGetUniformLocation(shader, "ScreenHeight"), screenHeight);

    glUniform1i(glGetUniformLocation(shader, "Texture"), 0);
    glUniform1i(glGetUniformLocation(shader, "DitherMap"), 1);

    VAO mesh = vao_from_obj(
            "data/kawaii.obj",
            glGetAttribLocation(shader, "VertexPosition"),
            glGetAttribLocation(shader, "VertexTexcoord"),
            glGetAttribLocation(shader, "VertexNormal")
    );

    VAO flameMesh = vao_from_obj(
            "data/flame.obj",
            glGetAttribLocation(shader, "VertexPosition"),
            glGetAttribLocation(shader, "VertexTexcoord"),
            glGetAttribLocation(shader, "VertexNormal")
    );

    Texture meshTexture = load_texture("data/kawaii.png");
    Texture flameTexture = load_texture("data/flame.png");

    vector<DitherArr> dithers = {
            DitherArr{{0.0}},
            DitherArr{
                    {0.5, 1.0, 0.5, 0.0, 0.0, 0.0},
                    {1.0, 1.0, 1.0, 0.0, 0.0, 0.0},
                    {0.5, 1.0, 0.5, 0.0, 0.0, 0.0},
                    {0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
                    {0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
                    {0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
            },
            DitherArr{
                    {0.5, 1.0, 0.5, 0.0, 0.0, 0.0},
                    {1.0, 1.0, 1.0, 0.0, 0.0, 0.0},
                    {0.5, 1.0, 0.5, 0.0, 0.0, 0.0},
                    {0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
                    {0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
                    {0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
            },
            DitherArr{
                    {0.5, 1.0, 0.5, 0.5, 0.0, 0.5},
                    {1.0, 1.0, 1.0, 0.0, 0.0, 0.0},
                    {0.5, 1.0, 0.5, 0.5, 0.0, 0.5},
                    {0.5, 0.0, 0.5, 0.5, 1.0, 0.5},
                    {0.0, 0.0, 0.0, 1.0, 1.0, 1.0},
                    {0.5, 0.0, 0.5, 0.5, 1.0, 0.5},
            },
            DitherArr{
                    {0.5, 1.0, 0.5, 0.5, 0.0, 0.5},
                    {1.0, 1.0, 1.0, 0.0, 0.0, 0.0},
                    {0.5, 1.0, 0.5, 0.5, 0.0, 0.5},
                    {0.5, 0.0, 0.5, 0.5, 1.0, 0.5},
                    {0.0, 0.0, 0.0, 1.0, 1.0, 1.0},
                    {0.5, 0.0, 0.5, 0.5, 1.0, 0.5},
            },
            DitherArr{
                    {1.0, 1.0, 1.0, 1.0, 1.0, 1.0},
                    {1.0, 1.0, 1.0, 1.0, 1.0, 1.0},
                    {1.0, 1.0, 1.0, 1.0, 1.0, 1.0},
                    {0.5, 0.0, 0.5, 1.0, 1.0, 1.0},
                    {0.0, 0.0, 0.0, 1.0, 1.0, 1.0},
                    {0.5, 0.0, 0.5, 1.0, 1.0, 1.0},
            },
            DitherArr{
                    {1.0, 1.0, 1.0, 1.0, 1.0, 1.0},
                    {1.0, 1.0, 1.0, 1.0, 1.0, 1.0},
                    {1.0, 1.0, 1.0, 1.0, 1.0, 1.0},
                    {0.5, 0.0, 0.5, 1.0, 1.0, 1.0},
                    {0.0, 0.0, 0.0, 1.0, 1.0, 1.0},
                    {0.5, 0.0, 0.5, 1.0, 1.0, 1.0},
            },
            DitherArr{{1.0}},
    };
    Texture3D ditherMap = gen_dithermap(screenWidth, screenHeight, dithers);

    float fovy = 90.f;
    mat4 camProj = perspective(fovy, 4.f / 3.f, 0.01f, 100.f);
    mat4 camView = translate(mat4(1.f), vec3(0.f, -2.f, -6.f));
    mat4 modelPos = mat4(1.f);

    GLint camProjUniform = glGetUniformLocation(shader, "camProj");
    GLint camViewUniform = glGetUniformLocation(shader, "camView");
    GLint modelPosUniform = glGetUniformLocation(shader, "modelPos");

    vec3 lightPos = vec3(5, 3, 1);
    float lightRadius = 5.f;

    double last_time = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        double this_time = glfwGetTime();
        double delta = this_time - last_time;

        glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUniformMatrix4fv(camProjUniform, 1, GL_FALSE, value_ptr(camProj));
        glUniformMatrix4fv(camViewUniform, 1, GL_FALSE, value_ptr(camView));

        glUniform3fv(glGetUniformLocation(shader, "LightPos"), 1, value_ptr(lightPos));
        glUniform1f(glGetUniformLocation(shader, "LightRadius"), lightRadius);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, ditherMap.handle);

        glUniformMatrix4fv(modelPosUniform, 1, GL_FALSE, value_ptr(modelPos));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, meshTexture.handle);
        glBindVertexArray(mesh.handle);
        glDrawArrays(GL_TRIANGLES, 0, mesh.num_tris * 3);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);

        glUniformMatrix4fv(modelPosUniform, 1, GL_FALSE,
                           value_ptr(scale(translate(mat4(1.f), lightPos), vec3(lightRadius / 5.f))));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, flameTexture.handle);
        glBindVertexArray(flameMesh.handle);
        glDrawArrays(GL_TRIANGLES, 0, flameMesh.num_tris * 3);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);


        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, 0);

        glfwSwapBuffers(window);
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_SPACE) != GLFW_PRESS) {
            modelPos = rotate(modelPos, float(delta), vec3(0.f, 1.f, 0.f));
        }

        float camSpeed = delta * 2.f;

        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
            lightPos.x -= camSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
            lightPos.x += camSpeed;
        }

        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
            lightPos.y += camSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
            lightPos.y -= camSpeed;
        }

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            lightPos.z += camSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            lightPos.z -= camSpeed;
        }

        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            lightRadius += delta;
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            lightRadius -= delta;
        }

        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
            modelPos = translate(modelPos, vec3(0,delta,0));
        }
        if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS) {
            modelPos = translate(modelPos, vec3(0,-delta,0));
        }

        if (glfwGetKey(window, GLFW_KEY_KP_8) == GLFW_PRESS) {
            camView = rotate(camView, float(delta), vec3(1, 0, 0));
        }
        if (glfwGetKey(window, GLFW_KEY_KP_2) == GLFW_PRESS) {
            camView = rotate(camView, -float(delta), vec3(1, 0, 0));
        }

        if (glfwGetKey(window, GLFW_KEY_KP_4) == GLFW_PRESS) {
            camView = rotate(camView, -float(delta), vec3(0, 1, 0));
        }
        if (glfwGetKey(window, GLFW_KEY_KP_6) == GLFW_PRESS) {
            camView = rotate(camView, float(delta), vec3(0, 1, 0));
        }

        if (glfwGetKey(window, GLFW_KEY_PAGE_UP) == GLFW_PRESS) {
            fovy += delta;
        }
        if (glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS) {
            fovy -= delta;
        }

        camProj = perspective(fovy, 4.f / 3.f, 0.01f, 100.f);

        last_time = this_time;
    }

    glDeleteVertexArrays(1, &mesh.handle);
    glDeleteProgram(shader);
    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;
} catch (const exception &e) {
    cerr << e.what() << endl;
    glfwTerminate();
    return EXIT_FAILURE;
}
