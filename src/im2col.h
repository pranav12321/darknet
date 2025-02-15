#ifndef IM2COL_H
#define IM2COL_H

void im2col_cpu(float* data_im,
        int channels, int height, int width,
        int ksize, int stride, int pad, float* data_col);

#ifdef GPU

void im2col_gpu(float *im,
         int channels, int height, int width,
         int ksize, int stride, int pad,float *data_col);

#endif

#ifdef FTP

void im2col_cpu_ftp(float* data_im,
     int channels,  int height,  int width, int height_out, int width_out,
     int ksize,  int stride, int pad, float* data_col);

#endif

#endif
