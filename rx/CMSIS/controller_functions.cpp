#include "arm_math.h"

/**
  @addtogroup PID
  @{
 */

/**
  @brief         Initialization function for the floating-point PID Control.
  @param[in,out] S               points to an instance of the PID structure
  @param[in]     resetStateFlag
                   - value = 0: no change in state
                   - value = 1: reset state

  @par           Details
                   The <code>resetStateFlag</code> specifies whether to set state to zero or not. \n
                   The function computes the structure fields: <code>A0</code>, <code>A1</code> <code>A2</code>
                   using the proportional gain( \c Kp), integral gain( \c Ki) and derivative gain( \c Kd)
                   also sets the state variables to all zeros.
 */

void arm_pid_init_f32(
  arm_pid_instance_f32 * S,
  int32_t resetStateFlag)
{
  /* Derived coefficient A0 */
  S->A0 = S->Kp + S->Ki + S->Kd;

  /* Derived coefficient A1 */
  S->A1 = (-S->Kp) - ((float32_t) 2.0f * S->Kd);

  /* Derived coefficient A2 */
  S->A2 = S->Kd;

  /* Check whether state needs reset or not */
  if (resetStateFlag)
  {
    /* Reset state to zero, The size will be always 3 samples */
    memset(S->state, 0, 3U * sizeof(float32_t));
  }

}

/**
  @brief         Reset function for the floating-point PID Control.
  @param[in,out] S  points to an instance of the floating-point PID structure

  @par           Details
                   The function resets the state buffer to zeros.
 */

void arm_pid_reset_f32(
  arm_pid_instance_f32 * S)
{
  /* Reset state to zero, The size will be always 3 samples */
  memset(S->state, 0, 3U * sizeof(float32_t));
}
