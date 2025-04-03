################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/cr_startup_lpc17.c \
../src/main.c 

OBJS += \
./src/cr_startup_lpc17.o \
./src/main.o 

C_DEPS += \
./src/cr_startup_lpc17.d \
./src/main.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: MCU C Compiler'
	arm-none-eabi-gcc -DDEBUG -D__USE_CMSIS=CMSISv1p30_LPC17xx -D__CODE_RED -D__NEWLIB__ -I"F:\Studia\Sezon 4\wbudy\lpc17xx_xpr_bb_140609\Lib_CMSISv1p30_LPC17xx\inc" -I"F:\Studia\Sezon 4\wbudy\lpc17xx_xpr_bb_140609\Lib_EaBaseBoard\inc" -I"F:\Studia\Sezon 4\wbudy\lpc17xx_xpr_bb_140609\Lib_FatFs_SD\inc" -I"F:\Studia\Sezon 4\wbudy\lpc17xx_xpr_bb_140609\Lib_MCU\inc" -O0 -g3 -Wall -c -fmessage-length=0 -fno-builtin -ffunction-sections -mcpu=cortex-m3 -mthumb -D__NEWLIB__ -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


