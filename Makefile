CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Iimgui -Iimgui/backends $(shell pkg-config --cflags glfw3)
LIBS     := $(shell pkg-config --libs glfw3 gl) -ldl

# Dear ImGui core + GLFW/OpenGL3 backend (compiled once, cached as .o)
IMGUI_SRC := imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp \
             imgui/imgui_widgets.cpp imgui/imgui_demo.cpp \
             imgui/backends/imgui_impl_glfw.cpp imgui/backends/imgui_impl_opengl3.cpp

# your code
APP_SRC  := simulate_orders.cpp orderbook.cpp

OBJ := $(APP_SRC:.cpp=.o) $(IMGUI_SRC:.cpp=.o)

sim: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ) $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) sim
