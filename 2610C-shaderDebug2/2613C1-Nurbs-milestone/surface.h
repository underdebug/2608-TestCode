#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace nurbs
{
/*
 * ====================== 曲面生成流程 ======================
 *
 *   data()             interpolate()          mesh()
 *   数据点 D ----------> 控制点 C ------------> 线框顶点
 *   10 x 10             10 x 10                 |
 *      |                   |                    v
 *      +---- error() <-----+              SurfaceGeometry / Qt
 *                          |
 *                     evaluate(u,v)
 *                          |
 *                          v
 *                  曲面上的任意三维位置
 *
 *   曲面截面示意（仅示意形状，不代表具体数据）：
 *       __                         __
 *         \\                     //
 *           \\_________________//
 *
 *   阅读顺序：节点 -> 基函数 -> 线性求解 -> 控制点 -> 曲面求值。
 */
// 固定规模的双三次张量积 B 样条曲面：由 10×10 个数据点求控制点，再采样显示。
// 所有权重均为 1，因此它也是 NURBS 的一个特例；参数 u、v 的有效范围为 [0, 1]。
// 三维点/向量：加法和数乘供线性求解、曲面加权求和使用。
struct P
{
    double x = 0, y = 0, z = 0;
    P operator+(P b) const
    {
        return {x + b.x, y + b.y, z + b.z};
    }
    P operator*(double s) const
    {
        return {x * s, y * s, z * s};
    }
};
using Grid = std::array<std::array<P, 10>, 10>;        // 第一维对应 u，第二维对应 v。
using Matrix = std::array<std::array<double, 10>, 10>; // 插值系数矩阵。
/*
 * Grid 的索引方向（画的是参数网格，三维点的 z 高度可以不同）：
 *
 *                  j / v -->
 *             0       1           9
 *   i / u  0  o-------o--- ... ----o
 *     |       |       |           |
 *     v    1  o-------o--- ... ----o
 *             :       :           :
 *          9  o-------o--- ... ----o
 *
 * d[i][j] 是必须经过的数据点；c[i][j] 是决定曲面形状的控制点。
 * 同一索引的 d 和 c 通常坐标不同，不要直接把 d 当作 c 求值。
 */
// 10 个控制点、次数 p=3，需要 10+3+1=14 个节点。
// 两端各重复 4 次，内部节点为 2/9、3/9、…、7/9（均匀参数的节点平均结果）。
inline std::array<double, 14> knots()
{
    /*
     * 节点数组下标： 0  1  2  3 |  4   5   6   7   8   9 | 10 11 12 13
     * 节点值：       0  0  0  0 | 2/9 3/9 4/9 5/9 6/9 7/9|  1  1  1  1
     *               \\_______// \\_____________________// \\________//
     *                  左夹持          内部节点              右夹持
     *
     * 数据参数为 0,1/9,...,1。内部节点由连续三个内部参数求平均：
     * (1/9+2/9+3/9)/3=2/9，...，(6/9+7/9+8/9)/3=7/9。
     * 节点与数据参数作用不同，因此这里没有 1/9 和 8/9 节点。
     */
    std::array<double, 14> k{};
    for (int j = 1; j <= 6; ++j)
        k[j + 3] = double(j + 1) / 9;
    for (int j = 10; j < 14; ++j)
        k[j] = 1;
    return k;
}
// 计算参数 t 处的 10 个三次 B 样条基函数 N_{i,3}(t)。
inline std::array<double, 10> basis(double t)
{
    /*
     * 零次基函数是区间指示函数，左端包含、右端不包含：
     *
     *    1        +---------+
     *             |         |
     *    0 -------+---------+------------------> t
     *            k[i]     k[i+1]
     *
     * 三次基函数的局部支撑为 [k[i], k[i+4])，右端点另行处理。
     * 内部简单节点处的形状示意（端部重复节点的形状不同）：
     *
     *                  __
     *                /    \
     *    ___________/      \\___________ --> t
     *             k[i]    k[i+4]
     *
     * 每次递推只组合相邻两个上一阶基函数：
     * N(i,d) = (t-k[i]) / (k[i+d]-k[i]) * N(i,d-1)
     *        + (k[i+d+1]-t) / (k[i+d+1]-k[i+1]) * N(i+1,d-1)
     *
     * 阶数 d：       0 --> 1 --> 2 --> 3
     * 有效项数：   13 -->12 -->11 -->10
     * 在 [0,1] 内：各项非负，总和为 1；任意位置最多 4 项非零。
     */
    auto k = knots();
    std::array<double, 13> b{};
    if (t >= 1)
    {
        // 零次基函数采用左闭右开区间，右端点需单独处理。
        std::array<double, 10> r{};
        r[9] = 1;
        return r;
    }
    // 先建立零次基函数：落在对应节点区间内为 1，否则为 0。
    for (int i = 0; i < 13; ++i)
        b[i] = (t >= k[i] && t < k[i + 1]);
    // Cox–de Boor 递推，从零次升到三次；重复节点导致分母为零时，该项取零。
    // 从左向右原地更新，确保 b[i]、b[i+1] 仍是上一阶的值。
    for (int d = 1; d <= 3; ++d)
        for (int i = 0; i < 13 - d; ++i)
        {
            double a = k[i + d] - k[i], c = k[i + d + 1] - k[i + 1];
            b[i] = (a ? (t - k[i]) * b[i] / a : 0) + (c ? (k[i + d + 1] - t) * b[i + 1] / c : 0);
        }
    std::array<double, 10> r{};
    std::copy_n(b.begin(), 10, r.begin());
    return r;
}
// 求解 a*x=b：带部分主元选择的 Gauss–Jordan 消元。
// b 的每项是三维点，因此一次消元同时求解 x、y、z 三组右端项。
// 参数按值传入，消元不会修改调用方的矩阵和数据。
inline std::array<P, 10> solve(Matrix a, std::array<P, 10> b)
{
    /*
     * 增广矩阵示意；右端每个 P 都包含 x、y、z 三个分量：
     *
     *   [ a00 a01 ... | b0 ]     行交换、归一化、消元     [ 1 0 ... | c0 ]
     *   [ a10 a11 ... | b1 ] -------------------------> [ 0 1 ... | c1 ]
     *   [ ... ... ... | .. ]                            [ ... ... | .. ]
     *
     * 第 i 轮：在第 i 列选主元 -> 移到第 i 行 -> 主元变为 1
     *          -> 其他行第 i 列变为 0。
     * 系数矩阵近似奇异时抛异常，因为无法可靠确定唯一控制点。
     */
    for (int i = 0; i < 10; ++i)
    {
        int p = i;
        // 在当前列选绝对值最大的主元，降低除以很小数造成的数值误差。
        for (int j = i + 1; j < 10; ++j)
            if (std::abs(a[j][i]) > std::abs(a[p][i]))
                p = j;
        if (std::abs(a[p][i]) < 1e-14)
            throw std::runtime_error("Singular interpolation matrix");
        std::swap(a[i], a[p]);
        std::swap(b[i], b[p]);
        double d = a[i][i];
        // 主元行归一化，再消去其他行的当前列，最终左侧变成单位矩阵。
        for (int j = i; j < 10; ++j)
            a[i][j] /= d;
        b[i] = b[i] * (1 / d);
        for (int r = 0; r < 10; ++r)
            if (r != i)
            {
                double f = a[r][i];
                for (int j = i; j < 10; ++j)
                    a[r][j] -= f * a[i][j];
                b[r] = b[r] + b[i] * (-f);
            }
    }
    return b;
}
// 生成演示用的 100 个插值数据点；x、y ∈ [-1,1]，z 表示起伏高度。
inline Grid data()
{
    Grid d;
    for (int i = 0; i < 10; ++i)
        for (int j = 0; j < 10; ++j)
        {
            double x = 2. * i / 9 - 1, y = 2. * j / 9 - 1;
            d[i][j] = {x, y, 0.35 * std::sin(3 * x) * std::cos(3 * y) + 0.15 * x * y};
        }
    return d;
}
// 求控制网格 c，使 S(i/9,j/9)=d[i][j]；数据点通常不等于控制点。
// 同一节点向量用于两个方向，A[row][col]=N_{col,3}(row/9)。
// 矩阵关系为 D=A*C*A^T，通过两个方向的线性求解得到 C，无需显式求逆。
inline Grid interpolate(const Grid &d)
{
    /*
     * 每行 A 表示一个采样参数位置，每列表示一个基函数：
     *
     *          N0       N1       ...      N9
     *   u=0   [1        0        ...      0 ]
     *   u=1/9 [N0(1/9)  N1(1/9)  ... N9(1/9)]
     *    ...  [             ...             ]
     *   u=1   [0        0        ...      1 ]
     *
     * 两次一维求解，等价于一次二维张量积插值：
     *
     *     D                       T                       C
     *   o o o                   o o o                   o o o
     *   | | |   沿每列 u 求解   | | |   沿每行 v 求解   -----
     *   o o o ----------------> o o o ----------------> o o o
     *   | | |     A*T=D         | | |     C*A^T=T        -----
     *   o o o                   o o o                   o o o
     *
     * T 是中间结果，只完成 u 方向的求解；C 才是最终控制网格。
     * A^{-T} 表示 (A^{-1})^T，代码通过 solve 实现，不实际构造逆矩阵。
     */
    Matrix a;
    for (int i = 0; i < 10; ++i)
        a[i] = basis(double(i) / 9);
    Grid t, c;
    // 固定 v 的数据索引 j，沿 u 求解：T=A^{-1}D。
    for (int j = 0; j < 10; ++j)
    {
        std::array<P, 10> b;
        for (int i = 0; i < 10; ++i)
            b[i] = d[i][j];
        b = solve(a, b);
        for (int i = 0; i < 10; ++i)
            t[i][j] = b[i];
    }
    // 对 T 的每一行沿 v 求解：C=T*A^{-T}。
    for (int i = 0; i < 10; ++i)
        c[i] = solve(a, t[i]);
    return c;
}
// 张量积求值：S(u,v)=Σ_i Σ_j c[i][j]*N_{i,3}(u)*N_{j,3}(v)。
// 权重全为 1，参数域内基函数之和为 1，NURBS 的分母因此为 1。
inline P evaluate(const Grid &c, double u, double v)
{
    /*
     * 两个方向的权重相乘，形成二维权重：w[i][j]=a[i]*b[j]。
     *
     *                    v 方向 b[j]
     *                b0      b1      ...
     *   u 方向 a0  [a0*b0   a0*b1     ...]
     *          a1  [a1*b0   a1*b1     ...]
     *          ... [          ...       ]
     *
     * P = sum(c[i][j]*w[i][j])，是控制点的加权平均。
     * 三次基函数局部支撑使一个位置最多受 4x4=16 个控制点影响；
     * 当前代码为直观起见仍遍历全部 100 个控制点，零权重项不贡献结果。
     * 夹持节点保证四个参数角点分别等于控制网格的四个角点。
     */
    auto a = basis(u), b = basis(v);
    P p;
    for (int i = 0; i < 10; ++i)
        for (int j = 0; j < 10; ++j)
            p = p + c[i][j] * (a[i] * b[j]);
    return p;
}
// 返回原始 100 个参数位置上的最大欧氏距离误差，不衡量采样线框的离散误差。
inline double error(const Grid &d, const Grid &c)
{
    // 每个数据位置：残差 = 曲面点 - 数据点；距离 = sqrt(dx²+dy²+dz²)。
    // 插值正确时误差应接近浮点舍入量级，而非要求浮点计算严格等于零。
    double e = 0;
    for (int i = 0; i < 10; ++i)
        for (int j = 0; j < 10; ++j)
        {
            P p = evaluate(c, double(i) / 9, double(j) / 9) + d[i][j] * (-1);
            e = std::max(e, std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z));
        }
    return e;
}
struct Vertex
{
    float x, y, z, r, g, b; // 顶点位置和 RGB 颜色，供显示层使用。
};
// 生成线框顶点列表，每两个顶点构成一条独立线段，不生成三角形面片。
inline std::vector<Vertex> mesh(const Grid &d, const Grid &c)
{
    /*
     * 参数域线框（映射到三维后，直线网格变成弯曲曲面上的折线）：
     *
     *   v=1  +---+---+---+     固定 u：沿 v 连接相邻采样点。
     *        |   |   |   |     固定 v：沿 u 连接相邻采样点。
     *        +---+---+---+
     *        |   |   |   |     每次 line(a,b) 写入两个独立顶点：
     *   v=0  +---+---+---+     [a,b, c,d, ...] -> 线段 ab、cd、...
     *       u=0         u=1
     *
     * 曲面：2*101*100=20200 条线段，40400 个顶点。
     * 数据点和控制点标记：各 100*3=300 条线段，共 1200 个顶点。
     * 总计 41600 个顶点。
     * 红色标记以数据点为中心：
     *             z
     *             |    y
     *             |   /
     *        -----o----- x
     *            /|
     *           / |
     * 半长 s=0.018，每条标记线总长 2*s=0.036（原始坐标单位）。
     */
    std::vector<Vertex> v;
    // 蓝青色曲面、红色数据点、绿色控制点；显示层根据颜色分量分组。
    auto line = [&](P a, P b, bool red = false, bool green = false)
    {
        for (P p : {a, b})
            v.push_back({float(p.x), float(p.y), float(p.z), red ? 1.f : 0.15f,
                         green ? 1.f : (red ? 0.2f : 0.75f), (red || green) ? 0.15f : 1.f});
    };
    constexpr int N = 100;
    // 两个参数方向各画 101 条等参数线，每条线由 100 段组成。
    for (int i = 0; i <= N; ++i)
        for (int j = 0; j < N; ++j)
        {
            line(evaluate(c, double(i) / N, double(j) / N),
                 evaluate(c, double(i) / N, double(j + 1) / N));
            line(evaluate(c, double(j) / N, double(i) / N),
                 evaluate(c, double(j + 1) / N, double(i) / N));
        }
    // 每个原始数据点用沿 x、y、z 的三条短线标记，便于观察插值是否经过它。
    for (auto row : d)
        for (auto p : row)
        {
            double s = .018;
            line(p + P{-s, 0, 0}, p + P{s, 0, 0}, true);
            line(p + P{0, -s, 0}, p + P{0, s, 0}, true);
            line(p + P{0, 0, -s}, p + P{0, 0, s}, true);
        }
    // 控制点使用稍大的绿色十字，便于区分重合的端点。
    for (const auto &row : c)
        for (const auto &p : row)
        {
            constexpr double s = .025;
            line(p + P{-s, 0, 0}, p + P{s, 0, 0}, false, true);
            line(p + P{0, -s, 0}, p + P{0, s, 0}, false, true);
            line(p + P{0, 0, -s}, p + P{0, 0, s}, false, true);
        }
    return v;
}
} // namespace nurbs
