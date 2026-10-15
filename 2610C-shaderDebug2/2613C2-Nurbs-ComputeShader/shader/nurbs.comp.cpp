#include <cstdio>
#include <cmath>
#include "shader.h"
#include "nurbs.comp"

int main()
{
    calculation.rows = 10;
    calculation.columns = 11;
    for (int row = 0; row < calculation.rows; ++row)
        for (int column = 0; column < calculation.columns; ++column)
        {
            float x = 2.0f * row / (calculation.rows - 1) - 1.0f;
            float y = 2.0f * column / (calculation.columns - 1) - 1.0f;
            float z = 0.35f * std::sin(3 * x) * std::cos(3 * y) + 0.15f * x * y;
            surface.inputPoint[row * calculation.columns + column] = vec4(100 * x, 100 * z, 100 * y, 1);
        }
    std::vector<vec4> positionStorage(43984);
    points.position = positionStorage.data();
    for (calculation.phase = 1; calculation.phase < 4; ++calculation.phase)
    {
        const unsigned count = calculation.phase == 1 ? calculation.columns :
                               calculation.phase == 2 ? calculation.rows : 43472u;
        for (gl_GlobalInvocationID.x = 0; gl_GlobalInvocationID.x < count;
             ++gl_GlobalInvocationID.x)
            shader();
        
    }
    
    return 0;
}
