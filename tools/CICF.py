import numpy as np
from scipy.optimize import curve_fit
import matplotlib.pyplot as plt

lower_bounds = [-10,  0]   # p1 ∈ [-10, 10], p2 ∈ [0, 100]
upper_bounds = [  0, 100]

def fit_two_stage_cic_compensation(
    N1, R1, M1,
    N2, R2, M2,
    num_points=300,
    initial_guess=(-3.0, 30.0),
    plot=False
):
    """
    拟合两级不同参数 CIC 串联的补偿滤波器参数 p1, p2，
    使得
        H_model(f) = H_target(f) + p1 * exp(p2*(f-0.5))
    其中
        H_target(f) = [sinc(R1*M1*f)]^{-N1} * [sinc(R2*M2*f)]^{-N2}

    参数:
      N1, R1, M1: 第一级 CIC 的级数、抽取率、微分延迟
      N2, R2, M2: 第二级 CIC 的级数、抽取率、微分延迟
      num_points: 拟合时使用的频率点数量
      initial_guess: (p1, p2) 的初始猜测
      plot: 是否绘制Result:比图

    返回:
      p_opt: 拟合出的 [p1, p2]
      p_cov: 协方差矩阵
    """
    # 避免 f=0 除零
    eps = 1e-8
    f = np.linspace(eps, 0.5, num_points)

    # 归一化 sinc：np.sinc(x) = sin(pi x)/(pi x)
    def sinc(x):
        return np.sinc(x)

    # 目标补偿响应：两个级联的倒数
    if N2 == 0:
        H_target = (sinc(R1 * M1 * f)**(-N1))
    else:
        H_target = (sinc(R1 * M1 * f)**(-N1)) * (sinc(R2 * M2 * f)**(-N2))

    # 模型函数：主补偿 + 指数项
    def H_model(f, p1, p2):
        return H_target + p1 * np.exp(p2 * (f - 0.5))

    # 拟合
    p_opt, p_cov = curve_fit(H_model, f, H_target, p0=initial_guess)

    if plot:
        plt.figure()
        plt.plot(f, H_target,    label="目标 $H_{target}(f)$")
        plt.plot(f, H_model(f, *p_opt), '--', label="拟合 $H_{model}(f)$")
        plt.xlabel("归一化频率 $f$")
        plt.ylabel("幅度")
        plt.title(
            f"两级 CIC 串联补偿拟合\n"
            f"Stage1: N={N1}, R={R1}, M={M1} | "
            f"Stage2: N={N2}, R={R2}, M={M2}"
        )
        plt.legend()
        plt.grid(True)
        plt.show()

    return p_opt, p_cov

if __name__ == "__main__":

    N1, R1, M1 = 5, 8192, 1

    p_opt, p_cov = fit_two_stage_cic_compensation(
        N1, R1, M1,
        0, 0 ,0
    )
    print(f"// WF CICF")
    print(f"\t{p_opt[0]:.4f}, {p_opt[1]:.4f},")

    N1, R1, M1 = 3, 256, 1
    N2, R2, M2 = 5, 40, 1

    for R2 in range(10, 41):
        p_opt, p_cov = fit_two_stage_cic_compensation(
            N1, R1, M1,
            N2, R2, M2,
        )
        SampleRate = 122.88 * 1000 / (R1 * R2)
        print(f"// R2={R2} SampleRate: {SampleRate:.2f} kHz")
        print(f"\t{p_opt[0]:.4f}, {p_opt[1]:.4f},")

    # 24K
    # p1 = -0.0046
    # p2 = 35.0944
    # 12K
    # p1 = -0.6364
    # p2 = 46.6379