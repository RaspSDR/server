# Design

## Summary

## Block Diagram

## Clocks
Whole system requires three clocks. The most critial clock is how to drive ADC with a clean and accurate clock. Trading off between cost, performance and complexity, there are three solutions. 1. A TCXO is most straight forward solution. However a good TCXO with 128.22Mhz is not cheap. Also it is hard to find such frequency. 2. Use a Si5351(MS5351) to generate 128.22Mhz clock from a 10M reference clock. 10M TCXO is easy to find and cheap. Si5351 is also common used in many SDR products. 3. Since there is nice PLL inside FPGA, we also considering to use internal PLL to drive ADC clock. This is most simple solution and elimate the needs of PLL chips.

We were measuring the accuracy of solutions and decided to take the option ?.

## Security

### Network security

### Default lock-in



## Minimize RX path latency


## Support Ethernet and Wifi


## 