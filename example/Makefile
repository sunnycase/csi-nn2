
c908_m1_f16:
	riscv64-unknown-linux-musl-gcc c908_mobilenetv1_f16.c -o c908_mobilenetv1_f16.elf -I../include ../install_nn2/lib/libshl_c908.a -lm -static -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -T ../k230_sdk/link.lds

c908_c2d_f32:
	riscv64-unknown-linux-musl-gcc c908_conv2d_f32.c -o c908_conv2d_f32.elf  -I../include ../install_nn2/lib/libshl_c908.a -lm -static -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -T ../k230_sdk/link.lds

clean:
	rm -rf *.elf
