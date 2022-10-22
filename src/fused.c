#include "ftp.h"
#include "darknet.h"


void partition_forward(network* net, 
                        int DEVICE_ID_X, int DEVICE_ID_Y,
                        network*** SHARED_NETWORKS,
                        float* COMBINED_INPUT_IMAGES,
                        int filter_size,
                        int start_x_forward, int start_y_forward,
                        int end_x_forward, int end_y_forward){

    int num_layers = net->n;

    int unit_boundry = ((filter_size & 0x1) == 1) ? ((filter_size - 1)/2) : (filter_size/2);
    int delta_boundry_edges_vertical = net->layers[0].out_h + unit_boundry;
    int delta_boundry_edges_horizontal = net->layers[0].out_w + unit_boundry;

    int left_boundry_edges = 0;
    int top_boundry_edges = 0;

    int right_boundry_edges = 0;
    int bottom_boundry_edges = 0;

    int boundry_frames = unit_boundry;

    int start_x_coordinate = start_x_forward;
    int start_y_coordinate = start_y_forward;
    int end_x_coordinate = end_x_forward;
    int end_y_coordinate = end_y_forward;

    net->layers[num_layers-1].left_boundry_edges_output = 0;
    net->layers[num_layers-1].top_boundry_edges_output = 0;
    net->layers[num_layers-1].right_boundry_edges_output = 0;
    net->layers[num_layers-1].bottom_boundry_edges_output = 0;

    net->layers[num_layers-1].delta_in_h_without_boundry = 3;//end_y_coordinate - start_y_coordinate + 1;
    net->layers[num_layers-1].delta_in_w_without_boundry = 3;//end_x_coordinate - start_x_coordinate + 1;

    net->layers[2].delta_in_h_without_boundry = 3;//end_y_coordinate - start_y_coordinate + 1;
    net->layers[2].delta_in_w_without_boundry = 3;//end_x_coordinate - start_x_coordinate + 1;

    net->layers[1].delta_in_h_without_boundry = 6;//end_y_coordinate - start_y_coordinate + 1;
    net->layers[1].delta_in_w_without_boundry = 6;//end_x_coordinate - start_x_coordinate + 1;

    net->layers[0].delta_in_h_without_boundry = 6;//end_y_coordinate - start_y_coordinate + 1;
    net->layers[0].delta_in_w_without_boundry = 6;//end_x_coordinate - start_x_coordinate + 1;

    for (int i = (num_layers-1); i >= 0; i--)
    {

        printf("Top boundry edges = %d\n", top_boundry_edges);
        printf("Left boundry edges = %d\n", left_boundry_edges);
        printf("Right boundry edges = %d\n", right_boundry_edges);
        printf("Bottom boundry edges = %d\n\n", bottom_boundry_edges);
        printf("Start x coordinate = %d\n", start_x_coordinate);
        printf("Start y coordinte = %d\n", start_y_coordinate);
        printf("End x coordinate = %d\n", end_x_coordinate);
        printf("End y coordinate = %d\n\n", end_y_coordinate);
        // if(i == 2 && DEVICE_ID_X == 1 && DEVICE_ID_Y == 0){
        //     printf("%d\n", net->layers[3].left_boundry_edges_output);
        //     while(1);
        // }
        int stride = net->layers[i].stride;
        int input_dim_x = net->layers[i].w;
        int input_dim_y = net->layers[i].h;

        int dilated_delta_dim_x = net->layers[i].out_w*stride;
        int dilated_delta_dim_y = net->layers[i].out_h*stride;

        left_boundry_edges += (unit_boundry*stride);
        top_boundry_edges = left_boundry_edges;

        right_boundry_edges += (unit_boundry*stride);
        bottom_boundry_edges = right_boundry_edges;

        net->layers[i].left_boundry_edges_output = left_boundry_edges;
        net->layers[i].top_boundry_edges_output = top_boundry_edges;
        net->layers[i].right_boundry_edges_output = right_boundry_edges;
        net->layers[i].bottom_boundry_edges_output = bottom_boundry_edges;


        start_x_coordinate = (start_x_coordinate*stride) - unit_boundry;
        start_y_coordinate = (start_y_coordinate*stride) - unit_boundry;

        end_x_coordinate = (end_x_coordinate*stride) + unit_boundry + stride - 1;
        end_y_coordinate = (end_y_coordinate*stride) + unit_boundry + stride - 1;

        net->layers[i].left_boundry_edges_output = net->layers[i].featuremap_start_coordinate_x - start_x_coordinate;
        net->layers[i].top_boundry_edges_output = net->layers[i].featuremap_start_coordinate_y - start_y_coordinate;
        net->layers[i].right_boundry_edges_output = end_x_coordinate - net->layers[i].featuremap_end_coordinate_x;
        net->layers[i].bottom_boundry_edges_output = end_y_coordinate - net->layers[i].featuremap_end_coordinate_y;


    }
    

    printf("Top boundry edges = %d\n", top_boundry_edges);
    printf("Left boundry edges = %d\n", left_boundry_edges);
    printf("Right boundry edges = %d\n", right_boundry_edges);
    printf("Bottom boundry edges = %d\n\n", bottom_boundry_edges);

    printf("Start x coordinate = %d\n", start_x_coordinate);
    printf("Start y coordinte = %d\n", start_y_coordinate);
    printf("End x coordinate = %d\n", end_x_coordinate);
    printf("End y coordinate = %d\n\n", end_y_coordinate);

    for (int i = start_y_coordinate; i <= end_y_coordinate; ++i)
    {
        for (int j = start_x_coordinate; j <= end_x_coordinate; ++j)
        {
            int pos = (i - start_y_coordinate)*(end_x_coordinate - start_x_coordinate + 1) + j - start_x_coordinate;
            if (i < 0 || i >= 12 || j < 0 || j >= 12)
            {
                net->input[pos] = 0.0;
            }
            else{
                net->input[pos] = COMBINED_INPUT_IMAGES[i*12  + j];                
            }
        }
    }

}

void partition_backward(network* net, 
                        int DEVICE_ID_X, int DEVICE_ID_Y,
                        network *** SHARED_NETWORKS,
                        float* COMBINED_EXP_DELTAS,
                        int filter_size,
                        int start_x_backward, int start_y_backward,
                        int end_x_backward, int end_y_backward){

    int unit_boundry = ((filter_size & 0x1) == 1) ? ((filter_size - 1)/2) : (filter_size/2);
    int delta_boundry_edges_vertical = net->layers[0].out_h + unit_boundry;
    int delta_boundry_edges_horizontal = net->layers[0].out_w + unit_boundry;

    int left_boundry_edges = 0;
    int top_boundry_edges = 0;

    int right_boundry_edges = 0;
    int bottom_boundry_edges = 0;

    int boundry_frames = unit_boundry;

    int start_x_coordinate = start_x_backward;
    int start_y_coordinate = start_y_backward;
    int end_x_coordinate = end_x_backward;
    int end_y_coordinate = end_y_backward;

    int num_layers = net->n;

    net->layers[0].left_boundry_edges_delta = 0;
    net->layers[0].top_boundry_edges_delta = 0;
    net->layers[0].right_boundry_edges_delta = 0;
    net->layers[0].bottom_boundry_edges_delta = 0;

    net->layers[0].delta_in_h_with_boundry = end_y_coordinate - start_y_coordinate + 1;
    net->layers[0].delta_in_w_with_boundry = end_x_coordinate - start_x_coordinate + 1;

    printf("DELTA H with boundry/without boundry = %d %d\n", net->layers[0].delta_in_h_with_boundry, net->layers[0].delta_in_h_without_boundry);
    printf("DELTA W with boundry/without boundry = %d %d\n", net->layers[0].delta_in_w_with_boundry, net->layers[0].delta_in_w_without_boundry);

    for (int i = 1; i < (num_layers); ++i)
    {

        printf("Top boundry edges = %d\n", top_boundry_edges);
        printf("Left boundry edges = %d\n", left_boundry_edges);
        printf("Right boundry edges = %d\n", right_boundry_edges);
        printf("Bottom boundry edges = %d\n\n", bottom_boundry_edges);
        printf("Start x coordinate = %d\n", start_x_coordinate);
        printf("Start y coordinte = %d\n", start_y_coordinate);
        printf("End x coordinate = %d\n", end_x_coordinate);
        printf("End y coordinate = %d\n\n", end_y_coordinate);


        int stride = net->layers[i].stride;
        int input_dim_x = net->layers[i].w;
        int input_dim_y = net->layers[i].h;

        int dilated_delta_dim_x = net->layers[i].out_w*stride;
        int dilated_delta_dim_y = net->layers[i].out_h*stride;

        left_boundry_edges += (unit_boundry/stride);
        top_boundry_edges = left_boundry_edges;

        right_boundry_edges += (unit_boundry + (stride - 1))/stride;
        bottom_boundry_edges = right_boundry_edges;

        net->layers[i].left_boundry_edges_delta = left_boundry_edges;
        net->layers[i].top_boundry_edges_delta = top_boundry_edges;
        net->layers[i].right_boundry_edges_delta = right_boundry_edges;
        net->layers[i].bottom_boundry_edges_delta = bottom_boundry_edges;

        if((start_x_coordinate - unit_boundry) > 0){
            start_x_coordinate = (start_x_coordinate - unit_boundry + (stride - 1))/stride;
        }
        else{
            start_x_coordinate = (start_x_coordinate - unit_boundry)/stride;         
        }

        if((end_x_coordinate + unit_boundry) < 0){
            end_x_coordinate = (end_x_coordinate + unit_boundry - (stride - 1))/stride;
        }
        else{
            end_x_coordinate = (end_x_coordinate + unit_boundry)/stride;          
        }


        if((start_y_coordinate - unit_boundry) > 0){
            start_y_coordinate = (start_y_coordinate - unit_boundry + (stride - 1))/stride;
        }
        else{
            start_y_coordinate = (start_y_coordinate - unit_boundry)/stride;          
        }

        if((end_y_coordinate + unit_boundry) < 0){
            end_y_coordinate = (end_y_coordinate + unit_boundry - (stride - 1))/stride;
        }
        else{
            end_y_coordinate = (end_y_coordinate + unit_boundry)/stride;          
        }

        net->layers[i].delta_in_h_with_boundry = end_y_coordinate - start_y_coordinate + 1;
        net->layers[i].delta_in_w_with_boundry = end_x_coordinate - start_x_coordinate + 1;

        net->layers[i].left_boundry_edges_delta = net->layers[i].delta_start_coordinate_x - start_x_coordinate;
        net->layers[i].top_boundry_edges_delta = net->layers[i].delta_start_coordinate_y - start_y_coordinate;
        net->layers[i].right_boundry_edges_delta = end_x_coordinate - net->layers[i].delta_end_coordinate_x;
        net->layers[i].bottom_boundry_edges_delta = end_y_coordinate - net->layers[i].delta_end_coordinate_y;

        printf("DELTA H with boundry/without boundry = %d %d\n", net->layers[i].delta_in_h_with_boundry, net->layers[i].delta_in_h_without_boundry);
        printf("DELTA W with boundry/without boundry = %d %d\n", net->layers[i].delta_in_w_with_boundry, net->layers[i].delta_in_w_without_boundry);

    }

    printf("Top boundry edges = %d\n", top_boundry_edges);
    printf("Left boundry edges = %d\n", left_boundry_edges);
    printf("Right boundry edges = %d\n", right_boundry_edges);
    printf("Bottom boundry edges = %d\n\n", bottom_boundry_edges);


    printf("Start x coordinate = %d\n", start_x_coordinate);
    printf("Start y coordinte = %d\n", start_y_coordinate);
    printf("End x coordinate = %d\n", end_x_coordinate);
    printf("End y coordinate = %d\n\n", end_y_coordinate);

    for (int i = start_y_coordinate; i <= end_y_coordinate; ++i)
    {
        for (int j = start_x_coordinate; j <= end_x_coordinate; ++j)
        {
            int pos = (i - start_y_coordinate)*(end_x_coordinate - start_x_coordinate + 1) + j - start_x_coordinate;
            if (i < 0 || i >= 6 || j < 0 || j >= 6)
            {
                net->layers[3].delta_with_boundry[pos]
                                            = 0.0;
            }
            else{
                net->layers[3].delta_with_boundry[pos]
                                            = COMBINED_EXP_DELTAS[i*6  + j];                
            }
        }
    }
}

void compute_tile_boundries(network* net,
                            int DEVICE_ID_X, int DEVICE_ID_Y,
                            network *** SHARED_NETWORKS,
                            float* COMBINED_INPUT_IMAGES, float* COMBINED_EXP_DELTAS,
                          int start_y_forward, int start_x_forward,
                          int end_y_forward, int end_x_forward,
                          int start_y_backward, int start_x_backward,
                          int end_y_backward, int end_x_backward){
	int num_layers_fuse = 3;
	int num_layers = 4;
	int filter_size = 3;//net->layers[0].size;

    partition_forward(net, DEVICE_ID_X, DEVICE_ID_Y,
                      SHARED_NETWORKS,
                      COMBINED_INPUT_IMAGES,
                      filter_size,
                      start_x_forward, start_y_forward,
                      end_x_forward, end_y_forward);
    partition_backward(net, DEVICE_ID_X, DEVICE_ID_Y,
                       SHARED_NETWORKS,
                       COMBINED_EXP_DELTAS,
                       filter_size,
                       start_x_backward, start_y_backward,
                       end_x_backward, end_y_backward);
}

