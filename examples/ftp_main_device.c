#include "fused_device.h"
#include "fused_convolution_device.h"

#include <stdio.h>
#include <stdlib.h>
#include "gemm.h"
#include "col2im.h"
#include "maxpool_layer.h"

#include "transport.h"
#include "fused_device.h"


extern network_config network_params_original;
extern network_config network_params_tile;
extern ftp_config ftp_params;

void backprop_layer0(network* net, float* INPUT_BOUNDRY);

int main_device(int argc, char* argv[]){

    // NUM_TILES_X = atoi(argv[1]);
    // NUM_TILES_Y = atoi(argv[2]);
    // DEVICE_ID_X = atoi(argv[19]);
    // DEVICE_ID_Y = atoi(argv[20]);

    // printf("%d %d %d %d\n", NUM_TILES_X, NUM_TILES_Y, DEVICE_ID_X, DEVICE_ID_Y);


    // train_groups_profile profile;

    // #ifdef SERVER
    //     init_server();
    // #else
    //     init_transport(argv);
    // #endif

    config_init(argc, argv);
    forward_pass();
    backward_pass();

    init_transport(argv);

    network* net;
    init_network(&net);



    float* INPUT_IMAGE = calloc(net->layers[0].featuremap_in_h_without_boundry*net->layers[0].featuremap_in_w_without_boundry*net->layers[0].c, sizeof(float));
    fill_cpu(net->layers[0].featuremap_in_h_without_boundry*net->layers[0].featuremap_in_w_without_boundry*net->layers[0].c, 0.1, INPUT_IMAGE, 1);

    printf("%d %d %d\n", net->layers[0].featuremap_in_h_without_boundry, net->layers[0].featuremap_in_w_without_boundry, net->layers[0].c);


    printf("%d %d %d %d %d\n", net->n, net->layers[net->n - 1].outputs, net->layers[net->n - 1].out_h, net->layers[net->n - 1].out_w, net->layers[net->n - 1].n);
    float* OUTPUT_DELTA = calloc(net->layers[net->n - 1].outputs, sizeof(float));
    fill_cpu(net->layers[net->n - 1].outputs, 0.1, OUTPUT_DELTA, 1);
    fill_cpu(net->layers[net->n - 1].outputs, 0.1, net->layers[net->n - 1].delta, 1);

    net->inputs = net->layers[0].featuremap_in_h_with_boundry*
                    net->layers[0].featuremap_in_w_with_boundry*
                    net->layers[0].c;

    float* INPUT_BOUNDRY = calloc(net->inputs, sizeof(float));


    for (int g = 0; g < ftp_params.NUM_GROUPS_FORWARD; ++g)
    {

        int group_start_idx = ftp_params.sync_group_vector_forward[g];
        int group_end_idx = (g == (ftp_params.NUM_GROUPS_FORWARD - 1)) ? (net->n - 1) : (ftp_params.sync_group_vector_forward[g+1] - 1);
        net->inputs = net->layers[group_start_idx].featuremap_in_h_with_boundry*
                        net->layers[group_start_idx].featuremap_in_w_with_boundry*
                        net->layers[group_start_idx].c;

        printf("idx bounds %d %d %d %d %d %d\n", group_start_idx, group_end_idx, net->layers[group_start_idx].featuremap_in_h_with_boundry, net->layers[group_start_idx].featuremap_in_w_with_boundry, net->layers[group_start_idx].c, net->inputs);

        net->input = calloc(net->inputs, sizeof(float));

       // printf("%d %d %d %d\n", net->layers[group_start_idx].top_boundry_edges_featuremap, net->layers[group_start_idx].bottom_boundry_edges_featuremap, net->layers[group_start_idx].right_boundry_edges_featuremap, 
        //                net->layers[group_start_idx].left_boundry_edges_featuremap);

        //get boundry data here
        assemble_forward_group_data_device(net, 
                                        INPUT_IMAGE,
                                        ftp_params.NUM_TILES_X, ftp_params.NUM_TILES_Y,
                                         group_start_idx,
                                         ftp_params.DEVICE_ID_X, ftp_params.DEVICE_ID_Y
                                         );
        if(g == 0){
            memcpy(INPUT_BOUNDRY, net->input, net->inputs*sizeof(float));
        }

        printf("Received input boundary. Starting inference\n");

        for (int l = group_start_idx; l <= group_end_idx; ++l)
        {

            zero_out_edges_featuremap_device(net, l, ftp_params.NUM_TILES_Y, ftp_params.NUM_TILES_X, ftp_params.DEVICE_ID_Y, ftp_params.DEVICE_ID_X);
            zero_out_spurious_edges_featuremap(net, l);
            net->index = l;

            if(net->layers[l].type == CONVOLUTIONAL){
                forward_convolutional_layer(net->layers[l], *net);
            }

            else if(net->layers[l].type == MAXPOOL){
                forward_maxpool_layer(net->layers[l], *net);
            }

            // for (int i = 0; i < net->layers[l].out_h; ++i)
            // {
            //     for (int j = 0; j < net->layers[l].out_w; ++j)
            //     {
            //         printf("%.4f ", net->layers[l].output[(i*net->layers[l].out_w) + j]);
            //     }
            //     printf("\n");
            // }
            // printf("\n");

            if(l == group_start_idx){
                free(net->input);
            }

            net->input = net->layers[l].output;
        }
    }
    // zero_out_spurious_edges_featuremap(net, net->n);

    for (int i = 0; i < net->layers[10].out_h; ++i)
    {
        for (int j = 0; j < net->layers[10].out_w; ++j)
        {
            printf("%.4f ", net->layers[10].output[(i*net->layers[10].out_w) + j]);
        }
        printf("\n");
    }

    printf("Inference complete\n");

    for (int g = (ftp_params.NUM_GROUPS_BACKWARD-1); g >= 0; --g)
    {

        int group_start_idx = (g == 0) ? (1) : (ftp_params.sync_group_vector_backward[g-1] + 1);
        int group_end_idx = ftp_params.sync_group_vector_backward[g];

        assemble_backward_group_data_device(net, 
                                        OUTPUT_DELTA,
                                        ftp_params.NUM_TILES_X, ftp_params.NUM_TILES_Y,
                                         group_end_idx,
                                         ftp_params.DEVICE_ID_X, ftp_params.DEVICE_ID_Y,
                                         net->n
                                         );


        printf("Received delta boundary. Starting backprop\n");

    //     int start_idx = (g==0) ? 1 : profile.bp[g].layer_start_idx;
        for (int l = group_end_idx; l >= group_start_idx; --l)
        {
            printf("propagating at Layer %d\n", l);

            int unit_boundry = (net->layers[l].size / 2);

            int featuremap_without_boundry_width = net->layers[l].featuremap_in_w_without_boundry + (2*unit_boundry);
            int featuremap_without_boundry_height = net->layers[l].featuremap_in_h_without_boundry + (2*unit_boundry);



            for (int c = 0; c < net->layers[l].c; ++c)
            {
                for (int m = 0; m < featuremap_without_boundry_height; ++m)
                {
                    for (int n = 0; n < featuremap_without_boundry_width; ++n)
                    {
                        int left_edges = net->layers[l].left_boundry_edges_featuremap;
                        int right_edges = net->layers[l].right_boundry_edges_featuremap;
                        int top_edges = net->layers[l].top_boundry_edges_featuremap;
                        int bottom_edges = net->layers[l].bottom_boundry_edges_featuremap;

                        int x_dim_nob = net->layers[l].featuremap_in_w_with_boundry;
                        int y_dim_nob = net->layers[l].featuremap_in_h_with_boundry;

                        net->workspace[(c*featuremap_without_boundry_width*featuremap_without_boundry_height) + m*featuremap_without_boundry_width + n] = 
                            net->layers[l-1].output[(c*x_dim_nob*y_dim_nob) + (m+top_edges - unit_boundry)*(x_dim_nob) + n + left_edges - unit_boundry];

                    }
                }
            }



            memcpy(net->layers[l-1].output, net->workspace, net->layers[l].c*featuremap_without_boundry_height*featuremap_without_boundry_width*sizeof(float));


            net->input = net->layers[l-1].output;
            net->delta = net->layers[l-1].delta;

            net->layers[l].w = (l == group_start_idx) ? net->layers[l-1].delta_in_w_without_boundry : net->layers[l-1].delta_in_w_with_boundry;
            net->layers[l].h = (l == group_start_idx) ? net->layers[l-1].delta_in_h_without_boundry : net->layers[l-1].delta_in_h_with_boundry;
            net->layers[l].out_w = net->layers[l].delta_in_w_with_boundry;
            net->layers[l].out_h = net->layers[l].delta_in_h_with_boundry;

            net->layers[l].pad = net->layers[l].size - 1;

            printf("dims: %d %d %d %d\n", net->layers[l].w, net->layers[l].h, net->layers[l].out_w, net->layers[l].out_h);

            zero_out_edges_delta_device(net, l, ftp_params.NUM_TILES_Y, ftp_params.NUM_TILES_X, ftp_params.DEVICE_ID_Y, ftp_params.DEVICE_ID_X);
            zero_out_spurious_edges_delta(net, l);

            if(net->layers[l].type == CONVOLUTIONAL)
                backward_convolutional_layer_dist_delta(net->layers[l], *net);
            else if(net->layers[l].type == MAXPOOL)
                backward_maxpool_layer(net->layers[l], *net);

            // for (int m = 0; m < net->layers[l].out_h; ++m)
            // {
            //     for (int n = 0; n < net->layers[l].out_w; ++n)
            //     {
            //         printf("%.2f ", net->layers[l].delta[m*net->layers[l].out_w + n]);
            //     }
            //     printf("\n");
                
            // }
            // printf("\n");

            // for (int m = 0; m < net->layers[l].h; ++m)
            // {
            //     for (int n = 0; n < net->layers[l].w; ++n)
            //     {
            //         printf("%.2f ", net->layers[l-1].delta[m*net->layers[l].w + n]);
            //     }
            //     printf("\n");
                
            // }
            // printf("\n");

            if(net->layers[l].type == CONVOLUTIONAL){

                int x_dim = net->layers[l].delta_in_w_without_boundry;
                int y_dim = net->layers[l].delta_in_h_without_boundry;
                int depth = net->layers[l].n;

                for (int c = 0; c < net->layers[l].n; ++c)
                {
                    int x_dim_nob = net->layers[l].delta_in_w_with_boundry;
                    int y_dim_nob = net->layers[l].delta_in_h_with_boundry;

                    for (int m = 0; m < net->layers[l].delta_in_h_without_boundry; ++m)
                    {
                        for (int n = 0; n < net->layers[l].delta_in_w_without_boundry; ++n)
                        {
                            int left_edges = net->layers[l].left_boundry_edges_delta;
                            int right_edges = net->layers[l].right_boundry_edges_delta;
                            int top_edges = net->layers[l].top_boundry_edges_delta;
                            int bottom_edges = net->layers[l].bottom_boundry_edges_delta;

                            net->workspace[(c*x_dim*y_dim) + m*net->layers[l].delta_in_w_without_boundry + n] = 
                                net->layers[l].delta[(c*x_dim_nob*y_dim_nob) + (m+top_edges)*(net->layers[l].delta_in_w_with_boundry) + n + left_edges];
                        }
                    }
                }

                memcpy(net->layers[l].delta, net->workspace, x_dim*y_dim*depth*sizeof(float));

                net->layers[l].out_w = net->layers[l].delta_in_w_without_boundry;
                net->layers[l].out_h = net->layers[l].delta_in_h_without_boundry;
                net->layers[l].h = featuremap_without_boundry_height;
                net->layers[l].w = featuremap_without_boundry_width;

                net->layers[l].pad = 0;

                net->index = l;

                printf("filter layer %d \n", l);

                backward_convolutional_layer_dist_filters(net->layers[l], *net);

            }

        }

    }

    backprop_layer0(net, INPUT_BOUNDRY);

    free(OUTPUT_DELTA);

    printf("Backprop complete\n");


            if(ftp_params.DEVICE_ID_X == 0 && ftp_params.DEVICE_ID_Y == 0)
                receive_sum_broadcast_weight_updates(net, ftp_params.NUM_TILES_Y, ftp_params.NUM_TILES_X);
            else{
            //#ifdef CLIENT
                sync_weight_updates(net, ftp_params.NUM_TILES_Y, ftp_params.NUM_TILES_X);
            }



            for (int m = 0; m < 3; ++m)
            {
                for (int n = 0; n < 3; ++n)
                {
                    printf("%.2f ", net->layers[0].weight_updates[m*3 + n]);
                }
                printf("\n");
                
            }
            printf("\n");

            for (int m = 0; m < 3; ++m)
            {
                for (int n = 0; n < 3; ++n)
                {
                    printf("%.2f ", net->layers[0].weight_updates[9 + m*3 + n]);
                }
                printf("\n");
                
            }
            printf("\n");

            for (int m = 0; m < 3; ++m)
            {
                for (int n = 0; n < 3; ++n)
                {
                    printf("%.2f ", net->layers[0].weight_updates[18 + m*3 + n]);
                }
                printf("\n");
                
            }
            printf("\n");

            for (int m = 0; m < 3; ++m)
            {
                for (int n = 0; n < 3; ++n)
                {
                    printf("%.2f ", net->layers[0].weight_updates[27 + m*3 + n]);
                }
                printf("\n");
                
            }
            printf("\n");



            for (int m = 0; m < 3; ++m)
            {
                for (int n = 0; n < 3; ++n)
                {
                    printf("%.2f ", net->layers[2].weight_updates[m*3 + n]);
                }
                printf("\n");
                
            }
            printf("\n");

            for (int m = 0; m < 3; ++m)
            {
                for (int n = 0; n < 3; ++n)
                {
                    printf("%.2f ", net->layers[2].weight_updates[9 + m*3 + n]);
                }
                printf("\n");
                
            }
            printf("\n");

            for (int m = 0; m < 3; ++m)
            {
                for (int n = 0; n < 3; ++n)
                {
                    printf("%.2f ", net->layers[2].weight_updates[18 + m*3 + n]);
                }
                printf("\n");
                
            }
            printf("\n");

            for (int m = 0; m < 3; ++m)
            {
                for (int n = 0; n < 3; ++n)
                {
                    printf("%.2f ", net->layers[2].weight_updates[27 + m*3 + n]);
                }
                printf("\n");
                
            }
            printf("\n");


            for (int m = 0; m < 3; ++m)
            {
                for (int n = 0; n < 3; ++n)
                {
                    printf("%.2f ", net->layers[5].weight_updates[m*3 + n]);
                }
                printf("\n");
                
            }
            printf("\n");

            for (int m = 0; m < 3; ++m)
            {
                for (int n = 0; n < 3; ++n)
                {
                    printf("%.2f ", net->layers[5].weight_updates[9 + m*3 + n]);
                }
                printf("\n");
                
            }
            printf("\n");

            for (int m = 0; m < 3; ++m)
            {
                for (int n = 0; n < 3; ++n)
                {
                    printf("%.2f ", net->layers[5].weight_updates[18 + m*3 + n]);
                }
                printf("\n");
                
            }
            printf("\n");

            for (int m = 0; m < 3; ++m)
            {
                for (int n = 0; n < 3; ++n)
                {
                    printf("%.2f ", net->layers[5].weight_updates[27 + m*3 + n]);
                }
                printf("\n");
                
            }
            printf("\n");



            for (int m = 0; m < 3; ++m)
            {
                for (int n = 0; n < 3; ++n)
                {
                    printf("%.2f ", net->layers[8].weight_updates[m*3 + n]);
                }
                printf("\n");
                
            }
            printf("\n");

            for (int m = 0; m < 3; ++m)
            {
                for (int n = 0; n < 3; ++n)
                {
                    printf("%.2f ", net->layers[8].weight_updates[9 + m*3 + n]);
                }
                printf("\n");
                
            }
            printf("\n");

            for (int m = 0; m < 3; ++m)
            {
                for (int n = 0; n < 3; ++n)
                {
                    printf("%.2f ", net->layers[8].weight_updates[18 + m*3 + n]);
                }
                printf("\n");
                
            }
            printf("\n");

            for (int m = 0; m < 3; ++m)
            {
                for (int n = 0; n < 3; ++n)
                {
                    printf("%.2f ", net->layers[8].weight_updates[27 + m*3 + n]);
                }
                printf("\n");
                
            }
            printf("\n");


            for (int m = 0; m < 3; ++m)
            {
                for (int n = 0; n < 3; ++n)
                {
                    printf("%.4f ", net->layers[10].weight_updates[m*3 + n]);
                }
                printf("\n");
                
            }
            printf("\n");

            for (int m = 0; m < 3; ++m)
            {
                for (int n = 0; n < 3; ++n)
                {
                    printf("%.4f ", net->layers[10].weight_updates[9 + m*3 + n]);
                }
                printf("\n");
                
            }
            printf("\n");

            for (int m = 0; m < 3; ++m)
            {
                for (int n = 0; n < 3; ++n)
                {
                    printf("%.4f ", net->layers[10].weight_updates[18 + m*3 + n]);
                }
                printf("\n");
                
            }
            printf("\n");

            for (int m = 0; m < 3; ++m)
            {
                for (int n = 0; n < 3; ++n)
                {
                    printf("%.4f ", net->layers[10].weight_updates[27 + m*3 + n]);
                }
                printf("\n");
                
            }
            printf("\n");

           FILE *fptr;

           fptr = fopen("weights_device.txt","w");

           if(fptr == NULL)
           {
              printf("Error!");   
              exit(1);             
           }
           int layer_cumulative_weights = 0;

            for (int l = 0; l < net->n; ++l){

                int num_filters = net->layers[l].n;
                int filter_size = net->layers[l].size;
                int channels = net->layers[l].c;

                for (int c = 0; c < channels; ++c)
                {
                    for (int f = 0; f < num_filters; ++f)
                    {                    
                        for (int m = 0; m < filter_size; ++m)
                        {
                            for (int n = 0; n < filter_size; ++n)
                            {
                             fprintf(fptr,"%.4f ", net->layers[l].weight_updates[(c*num_filters*filter_size*filter_size) + (f*filter_size*filter_size) + m*filter_size + n]);
                            }
                            fprintf(fptr, "\n");
                        }
                        fprintf(fptr, "\n");
                        fprintf(fptr, "\n");
                    }
                    fprintf(fptr, "\n");
                    fprintf(fptr, "\n");
                    fprintf(fptr, "\n");
                }

                fprintf(fptr, "\n");
                fprintf(fptr, "\n");
                fprintf(fptr, "\n");
                fprintf(fptr, "\n");

                layer_cumulative_weights += num_filters*channels*filter_size*filter_size;
            }

            fprintf(fptr, "\n");
            fprintf(fptr, "\n");
            fprintf(fptr, "\n");
            fprintf(fptr, "\n");
            fprintf(fptr, "\n");
            fprintf(fptr, "\n");
            fprintf(fptr, "\n");
            fprintf(fptr, "\n");

           fclose(fptr);

            printf("Done\n");

}

void backprop_layer0(network* net, float* INPUT_BOUNDRY){
    int unit_boundry = (net->layers[0].size / 2);

    int featuremap_without_boundry_width = net->layers[0].featuremap_in_w_without_boundry + (2*unit_boundry);
    int featuremap_without_boundry_height = net->layers[0].featuremap_in_h_without_boundry + (2*unit_boundry);



    for (int c = 0; c < net->layers[0].c; ++c)
    {
        for (int m = 0; m < featuremap_without_boundry_height; ++m)
        {
            for (int n = 0; n < featuremap_without_boundry_width; ++n)
            {
                int left_edges = net->layers[0].left_boundry_edges_featuremap;
                int right_edges = net->layers[0].right_boundry_edges_featuremap;
                int top_edges = net->layers[0].top_boundry_edges_featuremap;
                int bottom_edges = net->layers[0].bottom_boundry_edges_featuremap;

                int x_dim_nob = net->layers[0].featuremap_in_w_with_boundry;
                int y_dim_nob = net->layers[0].featuremap_in_h_with_boundry;

                net->workspace[(c*featuremap_without_boundry_width*featuremap_without_boundry_height) + m*featuremap_without_boundry_width + n] = 
                    INPUT_BOUNDRY[(c*x_dim_nob*y_dim_nob) + (m+top_edges - unit_boundry)*(x_dim_nob) + n + left_edges - unit_boundry];

            }
        }
    }

    memcpy(INPUT_BOUNDRY, net->workspace, net->layers[0].c*featuremap_without_boundry_height*featuremap_without_boundry_width*sizeof(float));
   
    net->layers[0].out_w = net->layers[0].delta_in_w_with_boundry;
    net->layers[0].out_h = net->layers[0].delta_in_h_with_boundry;
    net->layers[0].h = featuremap_without_boundry_height;
    net->layers[0].w = featuremap_without_boundry_width;

    net->layers[0].pad = 0;

    net->index = 0;

    // for (int m = 0; m < net->layers[0].out_h; ++m)
    // {
    //     for (int n = 0; n < net->layers[0].out_w; ++n)
    //     {
    //         printf("%.2f ", net->layers[0].delta[m*net->layers[0].out_w + n]);
    //     }
    //     printf("\n");
        
    // }
    // printf("\n");

    net->input = INPUT_BOUNDRY;

    printf("filter layer %d \n", 0);

    backward_convolutional_layer_dist_filters(net->layers[0], *net);   
}

int main(int argc, char* argv[]){
    main_device(argc, argv);
}
