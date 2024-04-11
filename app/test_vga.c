#include <stdio.h>
#include <math.h>

#define AD8370_STEPS 128
#define GAIN_SWEET_POINT 18
#define HIGH_GAIN_RATIO (0.409f)
#define LOW_GAIN_RATIO (0.059f)

static float hf_if_steps[AD8370_STEPS];

int main()
{
    for (int i = 0; i < AD8370_STEPS; i++)
    {
        if (i > GAIN_SWEET_POINT)
            hf_if_steps[i] = 20.0f * log10f(HIGH_GAIN_RATIO * (i - GAIN_SWEET_POINT + 3));
        else
            hf_if_steps[i] = 20.0f * log10f(LOW_GAIN_RATIO * (i + 1));

        printf("%f\t", hf_if_steps[i]);
    }

    printf("\n");

    return 0;
}