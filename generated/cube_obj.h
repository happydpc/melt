#pragma once

const static char s_cube_obj[] = R"(
o Cube
v 1.000000 -1.000000 -1.000000
v 1.000000 -1.000000 1.000000
v -1.000000 -1.000000 1.000000
v -1.000000 -1.000000 -1.000000
v 1.000000 1.000000 -0.999999
v 0.999999 1.000000 1.000001
v -1.000000 1.000000 1.000000
v -1.000000 1.000000 -1.000000
vn 0.000000 -1.000000 0.000000
vn 0.000000 1.000000 0.000000
vn 1.000000 -0.000000 0.000000
vn -0.000000 -0.000000 1.000000
vn -1.000000 -0.000000 -0.000000
vn 0.000000 0.000000 -1.000000
s off
f 2//1 3//1 4//1
f 8//2 7//2 6//2
f 1//3 5//3 6//3
f 2//4 6//4 7//4
f 7//5 8//5 4//5
f 1//6 4//6 8//6
f 1//1 2//1 4//1
f 5//2 8//2 6//2
f 2//3 1//3 6//3
f 3//4 2//4 7//4
f 3//5 7//5 4//5
f 5//6 1//6 8//6)";
