#include "fused_convolution_device.h"
#include "fused_device.h"
#include "transport.h"
#include "ftp.h"

extern int NUM_TILES_X;
extern int NUM_TILES_Y;
extern int DEVICE_ID_X;
extern int DEVICE_ID_Y;

extern network_config network_params_original;
extern network_config network_params_tile;
extern ftp_config ftp_params;

extern device_tile current_tile;
extern network_device current_device;
extern ftp_network ftp_cluster;

int cumulative = 0;

static void get_group_boundry_data_device(
    int NUM_TILES_X, int NUM_TILES_Y,
    float** device_data, 
    int rows, int cols, int depth, int batch,
    orientation region,
    int device_src_id_x, int device_src_id_y, 
    int device_dst_id_x, int device_dst_id_y) {

    float* boundry_data = calloc(rows*cols*depth*batch, sizeof(float));
    *device_data = boundry_data;
    //TODO: ASSERT CHECK DIM OVER/UNDERFLOW

    int x_dim;
    int y_dim; 

    float* boundry_src_data;

    if((device_src_id_x >= NUM_TILES_X) && 
        (region == BOTTOM_LEFT || region == LEFT || region == TOP_LEFT)){
        fill_cpu(rows*cols*depth*batch, 0, *device_data, 1);
        return;
    }
    if((device_src_id_x < 0) && 
        (region == TOP_RIGHT || region == RIGHT || region == BOTTOM_RIGHT)){
        fill_cpu(rows*cols*depth*batch, 0, *device_data, 1);
        return;
    }
    if((device_src_id_y >= NUM_TILES_Y) && 
        (region == TOP_LEFT || region == TOP || region == TOP_RIGHT)){
        fill_cpu(rows*cols*depth*batch, 0, *device_data, 1);
        return;
    }
    if((device_src_id_y < 0) && 
        (region == BOTTOM_LEFT || region == BOTTOM || region == BOTTOM_RIGHT)){
        fill_cpu(rows*cols*depth*batch, 0, *device_data, 1);
        return;
    }

    receive_boundry(boundry_data, rows*cols*depth*batch, device_src_id_x, device_src_id_y);

}

void clear_edges_featuremap_device(network* net, int layer_idx, int NUM_TILES_Y, int NUM_TILES_X, int device_id_y, int device_id_x){

    int x_dim = net->layers[layer_idx].featuremap_in_w_with_boundry;
    int y_dim = net->layers[layer_idx].featuremap_in_h_with_boundry;
    int depth = net->layers[layer_idx].c;
    int total_tile_sample_size = x_dim*y_dim*depth;
    int batches = net->batch;

    if(layer_idx > 0){

        if(device_id_y == 0){
            int rows = net->layers[layer_idx].top_boundry_edges_featuremap;
            int cols = net->layers[layer_idx].featuremap_in_w_with_boundry;

            for(int sample_id = 0; sample_id < batches; sample_id++){
                for (int c = 0; c < depth; ++c)
                {
                    for (int m = 0; m < rows; ++m)
                    {
                        for (int n = 0; n < cols; ++n)
                        {
                            net->layers[layer_idx-1].output[(sample_id*total_tile_sample_size) + (c*x_dim*y_dim) + m*cols + n] = 0.0;
                        }
                    }
                }
            }
        }

        if(device_id_x == 0){

            for(int sample_id = 0; sample_id < batches; sample_id++){
                for (int c = 0; c < depth; ++c)
                {
                    for (int m = 0; m < net->layers[layer_idx].featuremap_in_h_with_boundry; ++m)
                    {
                        for (int n = 0; n < net->layers[layer_idx].left_boundry_edges_featuremap; ++n)
                        {
                            net->layers[layer_idx-1].output[(sample_id*total_tile_sample_size) + (c*x_dim*y_dim) + (m*net->layers[layer_idx].featuremap_in_w_with_boundry) + n] = 0.0;
                        }
                    }
                }
            }
        }

        if(device_id_y == (NUM_TILES_Y - 1)){

            for(int sample_id = 0; sample_id < batches; sample_id++){
                for (int c = 0; c < depth; ++c)
                {
                    for (int m = 0; m < net->layers[layer_idx].bottom_boundry_edges_featuremap; ++m)
                    {
                        for (int n = 0; n < net->layers[layer_idx].featuremap_in_w_with_boundry; ++n)
                        {
                            net->layers[layer_idx-1].output[(sample_id*total_tile_sample_size) + (c*x_dim*y_dim) + (m+net->layers[layer_idx].featuremap_in_h_without_boundry+net->layers[layer_idx].top_boundry_edges_featuremap)*net->layers[layer_idx].featuremap_in_w_with_boundry + n] = 0.0;
                        }
                    }
                }
            }
        }

        if(device_id_x == (NUM_TILES_X - 1)){

            for(int sample_id = 0; sample_id < batches; sample_id++){
                for (int c = 0; c < depth; ++c)
                {
                    for (int m = 0; m < net->layers[layer_idx].featuremap_in_h_with_boundry; ++m)
                    {
                        for (int n = 0; n < net->layers[layer_idx].right_boundry_edges_featuremap; ++n)
                        {
                            net->layers[layer_idx-1].output[(sample_id*total_tile_sample_size) + (c*x_dim*y_dim) + (m)*net->layers[layer_idx].featuremap_in_w_with_boundry + n + net->layers[layer_idx].featuremap_in_w_without_boundry+net->layers[layer_idx].left_boundry_edges_featuremap] = 0.0;
                        }
                    }
                }
            }
        }

    }    
}



void clear_spurious_edges_featuremap(network* net, int layer_idx){

    int x_dim = net->layers[layer_idx].featuremap_in_w_with_boundry;
    int y_dim = net->layers[layer_idx].featuremap_in_h_with_boundry;
    int depth = net->layers[layer_idx].c;
    int total_tile_sample_size = x_dim*y_dim*depth;
    int batches = net->batch;

    int device_id_y = ftp_params.DEVICE_ID_Y;
    int device_id_x = ftp_params.DEVICE_ID_X;
    int NUM_TILES_X = ftp_params.NUM_TILES_X;
    int NUM_TILES_Y = ftp_params.NUM_TILES_Y;

    float* featuremap;

    // if(layer_idx > 0)
    //     featuremap = net->layers[layer_idx-1].output;
    // else
    featuremap = net->input;

    if(device_id_x == (NUM_TILES_X - 1)){

        int start_x_coordinate = network_params_tile.spurious_blocks[layer_idx].start_x_coordinate;
        int start_y_coordinate = network_params_tile.spurious_blocks[layer_idx].start_y_coordinate;

        if(start_x_coordinate > -1){
            for(int sample_id = 0; sample_id < batches; sample_id++){
                for (int c = 0; c < depth; ++c)
                {
                    for (int m = 0; m < net->layers[layer_idx].featuremap_in_h_with_boundry; ++m)
                    {
                        for (int n = net->layers[layer_idx].left_boundry_edges_featuremap + start_x_coordinate; n < net->layers[layer_idx].featuremap_in_w_with_boundry; ++n)
                        {
                            featuremap[(sample_id*total_tile_sample_size) + (c*x_dim*y_dim) + (m*net->layers[layer_idx].featuremap_in_w_with_boundry) + n] = 0.0;
                        }
                    }
                }
            }
        }
    }

    if(device_id_y == (NUM_TILES_Y - 1)){

        int start_x_coordinate = network_params_tile.spurious_blocks[layer_idx].start_x_coordinate;
        int start_y_coordinate = network_params_tile.spurious_blocks[layer_idx].start_y_coordinate;

        if(start_y_coordinate > -1){

            for(int sample_id = 0; sample_id < batches; sample_id++){
                for (int c = 0; c < depth; ++c)
                {
                    for (int m = net->layers[layer_idx].top_boundry_edges_featuremap + start_y_coordinate; m < net->layers[layer_idx].featuremap_in_h_with_boundry; ++m)
                    {
                        for (int n = 0; n < net->layers[layer_idx].featuremap_in_w_with_boundry; ++n)
                        {
                            featuremap[(sample_id*total_tile_sample_size) + (c*x_dim*y_dim) + (m*net->layers[layer_idx].featuremap_in_w_with_boundry) + n] = 0.0;
                        }
                    }
                }
            }
        }
    }
   
}


void clear_edges_delta_device(network* net, int layer_idx, int NUM_TILES_Y, int NUM_TILES_X, int device_id_y, int device_id_x){

    int x_dim = net->layers[layer_idx].delta_in_w_with_boundry;
    int y_dim = net->layers[layer_idx].delta_in_h_with_boundry;
    int depth = (net->layers[layer_idx].type == CONVOLUTIONAL) ? net->layers[layer_idx].n : net->layers[layer_idx].c;
    int total_tile_sample_size = x_dim*y_dim*depth;
    int batches = net->batch;

    if(layer_idx > 0){

        if(device_id_y == 0){

            for(int sample_id = 0; sample_id < batches; sample_id++){
                for (int c = 0; c < depth; ++c)
                {
                    for (int m = 0; m < net->layers[layer_idx].top_boundry_edges_delta; ++m)
                    {
                        for (int n = 0; n < net->layers[layer_idx].delta_in_w_with_boundry; ++n)
                        {
                            net->layers[layer_idx].delta[(sample_id*total_tile_sample_size) + (c*x_dim*y_dim) + m*net->layers[layer_idx].delta_in_w_with_boundry + n] = 0.0;
                        }
                    }
                }
            }
        }

        if(device_id_x == 0){

            for(int sample_id = 0; sample_id < batches; sample_id++){
                for (int c = 0; c < depth; ++c)
                {
                    for (int m = 0; m < net->layers[layer_idx].delta_in_h_with_boundry; ++m)
                    {
                        for (int n = 0; n < net->layers[layer_idx].left_boundry_edges_delta; ++n)
                        {
                            net->layers[layer_idx].delta[(sample_id*total_tile_sample_size) + (c*x_dim*y_dim) + (m*net->layers[layer_idx].delta_in_w_with_boundry) + n] = 0.0;
                        }
                    }
                }
            }
        }

        if(device_id_y == (NUM_TILES_Y - 1)){

            for(int sample_id = 0; sample_id < batches; sample_id++){
                for (int c = 0; c < depth; ++c)
                {
                    for (int m = 0; m < net->layers[layer_idx].bottom_boundry_edges_delta; ++m)
                    {
                        for (int n = 0; n < net->layers[layer_idx].delta_in_w_with_boundry; ++n)
                        {
                            net->layers[layer_idx].delta[(sample_id*total_tile_sample_size) + (c*x_dim*y_dim) + (m+net->layers[layer_idx].delta_in_h_without_boundry+net->layers[layer_idx].top_boundry_edges_delta)*net->layers[layer_idx].delta_in_w_with_boundry + n] = 0.0;
                        }
                    }
                }
            }
        }

        if(device_id_x == (NUM_TILES_X - 1)){

            for(int sample_id = 0; sample_id < batches; sample_id++){
                for (int c = 0; c < depth; ++c)
                {
                    for (int m = 0; m < net->layers[layer_idx].delta_in_h_with_boundry; ++m)
                    {
                        for (int n = 0; n < net->layers[layer_idx].right_boundry_edges_delta; ++n)
                        {
                            net->layers[layer_idx].delta[(sample_id*total_tile_sample_size) + (c*x_dim*y_dim) + (m)*net->layers[layer_idx].delta_in_w_with_boundry + n + net->layers[layer_idx].delta_in_w_without_boundry+net->layers[layer_idx].left_boundry_edges_delta] = 0.0;
                        }
                    }
                }
            }
        }

    }    
}


void clear_spurious_edges_delta(network* net, int layer_idx){

    int x_dim = net->layers[layer_idx].delta_in_w_with_boundry;
    int y_dim = net->layers[layer_idx].delta_in_h_with_boundry;
    int depth = (net->layers[layer_idx].type == CONVOLUTIONAL) ? net->layers[layer_idx].n : net->layers[layer_idx].c;
    int total_tile_sample_size = x_dim*y_dim*depth;
    int batches = net->batch;

    int device_id_y = ftp_params.DEVICE_ID_Y;
    int device_id_x = ftp_params.DEVICE_ID_X;
    int NUM_TILES_X = ftp_params.NUM_TILES_X;
    int NUM_TILES_Y = ftp_params.NUM_TILES_Y;

    if(device_id_x == (NUM_TILES_X - 1)){

        int start_x_coordinate = network_params_tile.spurious_blocks[layer_idx+1].start_x_coordinate;
        int start_y_coordinate = network_params_tile.spurious_blocks[layer_idx+1].start_y_coordinate;

        if(start_x_coordinate > -1){

            for(int sample_id = 0; sample_id < batches; sample_id++){
                for (int c = 0; c < depth; ++c)
                {
                    for (int m = 0; m < net->layers[layer_idx].delta_in_h_with_boundry; ++m)
                    {
                        for (int n = net->layers[layer_idx].left_boundry_edges_delta + start_x_coordinate; n < net->layers[layer_idx].delta_in_w_with_boundry; ++n)
                        {
                            net->layers[layer_idx].delta[(sample_id*total_tile_sample_size) + (c*x_dim*y_dim) + (m*net->layers[layer_idx].delta_in_w_with_boundry) + n] = 0.0;
                        }
                    }
                }
            }
        }
    }

    if(device_id_y == (NUM_TILES_Y - 1)){

        int start_x_coordinate = network_params_tile.spurious_blocks[layer_idx+1].start_x_coordinate;
        int start_y_coordinate = network_params_tile.spurious_blocks[layer_idx+1].start_y_coordinate;

        if(start_y_coordinate > -1){

            for(int sample_id = 0; sample_id < batches; sample_id++){
                for (int c = 0; c < depth; ++c)
                {
                    for (int m = net->layers[layer_idx].top_boundry_edges_delta + start_y_coordinate; m < net->layers[layer_idx].delta_in_h_with_boundry; ++m)
                    {
                        for (int n = 0; n < net->layers[layer_idx].delta_in_w_with_boundry; ++n)
                        {
                            net->layers[layer_idx].delta[(sample_id*total_tile_sample_size) + (c*x_dim*y_dim) + (m*net->layers[layer_idx].delta_in_w_with_boundry) + n] = 0.0;
                        }
                    }
                }
            }
        }
    }
}

void receive_sum_transmit_device_weight_updates(network* net, int NUM_TILES_Y, int NUM_TILES_X){

    for (int i = 0; i < net->n; ++i)
    {
        int total_filter_elements = net->layers[i].size*net->layers[i].size*net->layers[i].c*net->layers[i].n;

    }

    int total_weights = 0;
    for (int l = 0; l < net->n; ++l){
        int num_filters = net->layers[l].n;
        int filter_size = net->layers[l].size;
        int channels = net->layers[l].c;
        total_weights += num_filters*channels*filter_size*filter_size;
    }    

    int num_tiles_in_device = current_device.num_tiles;
    int total_devices = ftp_cluster.total_devices;

    printf("Tiles: Total Devices: %d %d\n", num_tiles_in_device, total_devices);

    for (int i = 1; i < num_tiles_in_device; ++i)
    {
        for (int l = 0; l < net->n; ++l){
            int num_filters = net->layers[l].n;
            int filter_size = net->layers[l].size;
            int channels = net->layers[l].c;

            float* tile_weight_updates_offset = (net->layers[l].weight_updates) + (i*num_filters*channels*filter_size*filter_size);

            for (int n = 0; n < (num_filters*channels*filter_size*filter_size); ++n)
            {
                net->layers[l].weight_updates[n] +=
                tile_weight_updates_offset[n];
                //printf("%.2f ", data[layer_cumulative_weights + ((c*num_filters*filter_size*filter_size) + (f*filter_size*filter_size) + m*filter_size + n)]);
            }
        }
    }

    float* data = calloc(total_weights, sizeof(float));
    for (int i = 1; i < total_devices; ++i)
    {
        receive_boundry(data, total_weights, (ftp_cluster.devices[i].representative_tile_network_id)%NUM_TILES_X, (ftp_cluster.devices[i].representative_tile_network_id)/NUM_TILES_X);

        int layer_cumulative_weights = 0;

        for (int l = 0; l < net->n; ++l){
            int num_filters = net->layers[l].n;
            int filter_size = net->layers[l].size;
            int channels = net->layers[l].c;
            for (int n = 0; n < (num_filters*channels*filter_size*filter_size); ++n)
            {
                net->layers[l].weight_updates[n] += 
                data[layer_cumulative_weights + n];

                //printf("%.2f ", data[layer_cumulative_weights + ((c*num_filters*filter_size*filter_size) + (f*filter_size*filter_size) + m*filter_size + n)]);
            }
            layer_cumulative_weights += (num_filters*channels*filter_size*filter_size);

        }

    }

    for (int i = 1; i < total_devices; ++i)
    {
        int layer_cumulative_weights = 0;

        for (int l = 0; l < net->n; ++l){
            int num_filters = net->layers[l].n;
            int filter_size = net->layers[l].size;
            int channels = net->layers[l].c;
            for (int n = 0; n < (num_filters*channels*filter_size*filter_size); ++n)
            {
                data[layer_cumulative_weights + n] = 
                net->layers[l].weight_updates[n];
                //printf("%.2f ", data[layer_cumulative_weights + ((c*num_filters*filter_size*filter_size) + (f*filter_size*filter_size) + m*filter_size + n)]);
            }
            layer_cumulative_weights += (num_filters*channels*filter_size*filter_size);

        }

        send_boundry(data, total_weights, (ftp_cluster.devices[i].representative_tile_network_id)%NUM_TILES_X, (ftp_cluster.devices[i].representative_tile_network_id)/NUM_TILES_X);

    }

    for (int i = 1; i < num_tiles_in_device; ++i)
    {
        for (int l = 0; l < net->n; ++l){
            int num_filters = net->layers[l].n;
            int filter_size = net->layers[l].size;
            int channels = net->layers[l].c;

            float* tile_weight_updates_offset = (net->layers[l].weight_updates) + (i*num_filters*channels*filter_size*filter_size);

            for (int n = 0; n < (num_filters*channels*filter_size*filter_size); ++n)
            {
                tile_weight_updates_offset[n] =
                net->layers[l].weight_updates[n];
                //printf("%.2f ", data[layer_cumulative_weights + ((c*num_filters*filter_size*filter_size) + (f*filter_size*filter_size) + m*filter_size + n)]);
            }
        }
    }

   filter_sync_complete_sema_post(num_tiles_in_device - 1);
}

void devices_send_partial_weight_updates(network* net, int NUM_TILES_Y, int NUM_TILES_X){

    int total_weights = 0;
    int num_tiles_in_device = current_device.num_tiles;
    int total_devices = ftp_cluster.total_devices;

    for (int l = 0; l < net->n; ++l){
        int num_filters = net->layers[l].n;
        int filter_size = net->layers[l].size;
        int channels = net->layers[l].c;
        total_weights += num_filters*channels*filter_size*filter_size;
    }    


    for (int i = 1; i < num_tiles_in_device; ++i)
    {
        for (int l = 0; l < net->n; ++l){
            int num_filters = net->layers[l].n;
            int filter_size = net->layers[l].size;
            int channels = net->layers[l].c;

            float* tile_weight_updates_offset = (net->layers[l].weight_updates) + (i*num_filters*channels*filter_size*filter_size);

            for (int n = 0; n < (num_filters*channels*filter_size*filter_size); ++n)
            {
                net->layers[l].weight_updates[n] +=
                tile_weight_updates_offset[n];
                //printf("%.2f ", data[layer_cumulative_weights + ((c*num_filters*filter_size*filter_size) + (f*filter_size*filter_size) + m*filter_size + n)]);
            }
        }
    }

    float* data = malloc(total_weights * sizeof(float));

    int layer_cumulative_weights = 0;
    for (int l = 0; l < net->n; ++l){

        int num_filters = net->layers[l].n;
        int filter_size = net->layers[l].size;
        int channels = net->layers[l].c;

        for (int n = 0; n < (num_filters*channels*filter_size*filter_size); ++n)
        {
            data[layer_cumulative_weights + n] = 
            net->layers[l].weight_updates[n];
        }
        layer_cumulative_weights += num_filters*channels*filter_size*filter_size;
    }

    send_boundry(data, total_weights, 0, 0);

    receive_boundry(data, total_weights, 0, 0);

    layer_cumulative_weights = 0;

    for (int l = 0; l < net->n; ++l){

        int num_filters = net->layers[l].n;
        int filter_size = net->layers[l].size;
        int channels = net->layers[l].c;

        for (int n = 0; n < (num_filters*channels*filter_size*filter_size); ++n)
        {
            net->layers[l].weight_updates[n] = 
            data[layer_cumulative_weights + n];
        }
        layer_cumulative_weights += num_filters*channels*filter_size*filter_size;
    }

    free(data);


    for (int i = 1; i < num_tiles_in_device; ++i)
    {
        for (int l = 0; l < net->n; ++l){
            int num_filters = net->layers[l].n;
            int filter_size = net->layers[l].size;
            int channels = net->layers[l].c;

            float* tile_weight_updates_offset = (net->layers[l].weight_updates) + (i*num_filters*channels*filter_size*filter_size);

            for (int n = 0; n < (num_filters*channels*filter_size*filter_size); ++n)
            {
                tile_weight_updates_offset[n] =
                net->layers[l].weight_updates[n];
                //printf("%.2f ", data[layer_cumulative_weights + ((c*num_filters*filter_size*filter_size) + (f*filter_size*filter_size) + m*filter_size + n)]);
            }
        }
    }

    filter_sync_complete_sema_post(num_tiles_in_device - 1);
}

void pos_correct_maxpool_indices(int* data, int stride,
                                int width_pool_in_forward,
                                int height_pool_out_backward, int width_pool_out_backward, int depth_pool_out_backward, int batch){
    for(int b = 0; b < batch; b++){
        for(int d = 0; d < depth_pool_out_backward; d++){
            for(int h = 0; h < height_pool_out_backward; h++){
                for(int w = 0; w < width_pool_out_backward; w++){
                    int sample_size_out_backward = depth_pool_out_backward*height_pool_out_backward*width_pool_out_backward;
                    int channel_size_out_backward = height_pool_out_backward*width_pool_out_backward;
                    int element = data[(b*sample_size_out_backward) + (d*channel_size_out_backward) + (h*width_pool_out_backward) + w];
                    int offset_x = element % stride;
                    int offset_y = ((element - (element % width_pool_in_forward)) / width_pool_in_forward) % stride;
                    data[(b*sample_size_out_backward) + (d*channel_size_out_backward) + (h*width_pool_out_backward) + w] = 
                    stride*stride*((b*sample_size_out_backward) + 
                    (d*channel_size_out_backward) + (h*width_pool_out_backward)) + w*stride + 
                    offset_x + (stride*width_pool_out_backward*offset_y);
                    
                }
            }
        }
    }
}

void assemble_pool_indices(network* net, int l, int device_id_x, int device_id_y, int NUM_TILES_X, int NUM_TILES_Y){

    int* tile_core_pool_indices = calloc(net->batch*net->layers[l].delta_in_h_with_boundry*net->layers[l].delta_in_w_with_boundry*net->layers[l].c, sizeof(int));
    
    int left_extra_edges_pool_backward = net->layers[l].left_boundry_edges_delta - 
                                         (net->layers[l].left_boundry_edges_featuremap / net->layers[l].stride);
    int right_extra_edges_pool_backward = net->layers[l].right_boundry_edges_delta - 
                                         (net->layers[l].right_boundry_edges_featuremap / net->layers[l].stride);
    int bottom_extra_edges_pool_backward = net->layers[l].bottom_boundry_edges_delta - 
                                         (net->layers[l].bottom_boundry_edges_featuremap / net->layers[l].stride);
    int top_extra_edges_pool_backward = net->layers[l].top_boundry_edges_delta - 
                                         (net->layers[l].top_boundry_edges_featuremap / net->layers[l].stride);

    int left_extra_edges_pool_forward = (net->layers[l].left_boundry_edges_featuremap / net->layers[l].stride) -
                                        net->layers[l].left_boundry_edges_delta;
    int right_extra_edges_pool_forward = (net->layers[l].right_boundry_edges_featuremap / net->layers[l].stride) -
                                        net->layers[l].right_boundry_edges_delta;
    int bottom_extra_edges_pool_forward = (net->layers[l].bottom_boundry_edges_featuremap / net->layers[l].stride) -
                                        net->layers[l].bottom_boundry_edges_delta;
    int top_extra_edges_pool_forward = (net->layers[l].top_boundry_edges_featuremap / net->layers[l].stride) -
                                        net->layers[l].top_boundry_edges_delta;
    
    printf("%d %d %d %d\n", left_extra_edges_pool_backward, right_extra_edges_pool_backward, bottom_extra_edges_pool_backward, top_extra_edges_pool_backward);

    if(left_extra_edges_pool_backward < 0)
        left_extra_edges_pool_backward = 0;
    if(right_extra_edges_pool_backward < 0)
        right_extra_edges_pool_backward = 0;
    if(top_extra_edges_pool_backward < 0)
        top_extra_edges_pool_backward = 0;
    if(bottom_extra_edges_pool_backward < 0)
        bottom_extra_edges_pool_backward = 0;

    if(left_extra_edges_pool_forward < 0)
        left_extra_edges_pool_forward = 0;
    if(right_extra_edges_pool_forward < 0)
        right_extra_edges_pool_forward = 0;
    if(top_extra_edges_pool_forward < 0)
        top_extra_edges_pool_forward = 0;
    if(bottom_extra_edges_pool_forward < 0)
        bottom_extra_edges_pool_forward = 0;

    copy_slice((float*) tile_core_pool_indices, net->layers[l].indexes, net->batch, net->layers[l].c,
        net->layers[l].featuremap_in_h_with_boundry/net->layers[l].stride,
        net->layers[l].featuremap_in_w_with_boundry/net->layers[l].stride,
        net->layers[l].delta_in_h_with_boundry, net->layers[l].delta_in_w_with_boundry,
        left_extra_edges_pool_forward, top_extra_edges_pool_forward, left_extra_edges_pool_backward, top_extra_edges_pool_backward,
        net->layers[l].delta_in_h_with_boundry - top_extra_edges_pool_backward - bottom_extra_edges_pool_backward,
        net->layers[l].delta_in_w_with_boundry - left_extra_edges_pool_backward - right_extra_edges_pool_backward,
        net->layers[l].delta_in_h_with_boundry - top_extra_edges_pool_backward - bottom_extra_edges_pool_backward,
        net->layers[l].delta_in_w_with_boundry - left_extra_edges_pool_backward - right_extra_edges_pool_backward,
        net->workspace);

    if((left_extra_edges_pool_backward > 0) || (right_extra_edges_pool_backward > 0) || (top_extra_edges_pool_backward > 0) || (bottom_extra_edges_pool_backward > 0)){
        assemble_tile(net, net->batch, net->layers[l].c,
                        tile_core_pool_indices, net->layers[l].indexes,
                        net->layers[l].delta_in_h_with_boundry - top_extra_edges_pool_backward - bottom_extra_edges_pool_backward,
                        net->layers[l].delta_in_w_with_boundry - left_extra_edges_pool_backward - right_extra_edges_pool_backward,
                        left_extra_edges_pool_backward, right_extra_edges_pool_backward, top_extra_edges_pool_backward, bottom_extra_edges_pool_backward,
                        device_id_x, device_id_y, NUM_TILES_X, NUM_TILES_Y);
    }

    pos_correct_maxpool_indices(tile_core_pool_indices, net->layers[l].stride,
                                    net->layers[l].featuremap_in_w_with_boundry,
                                    net->layers[l].delta_in_h_with_boundry, net->layers[l].delta_in_w_with_boundry, net->layers[l].c, net->batch);
    
    memcpy(net->layers[l].indexes, tile_core_pool_indices, net->batch*net->layers[l].delta_in_h_with_boundry*net->layers[l].delta_in_w_with_boundry*net->layers[l].c*sizeof(float));
    free(tile_core_pool_indices);    
}

void remove_extra_boundary_data(network* net, int l){
    int left_edges_extra = (net->layers[l].left_boundry_edges_delta * net->layers[l].stride) - net->layers[l-1].left_boundry_edges_delta;
    int top_edges_extra = (net->layers[l].top_boundry_edges_delta * net->layers[l].stride) - net->layers[l-1].top_boundry_edges_delta;
    int right_edges_extra = (net->layers[l].right_boundry_edges_delta * net->layers[l].stride) - net->layers[l-1].right_boundry_edges_delta;
    int bottom_edges_extra = (net->layers[l].bottom_boundry_edges_delta * net->layers[l].stride) - net->layers[l-1].bottom_boundry_edges_delta;

    if(left_edges_extra < 0)
        left_edges_extra = 0;
    if(right_edges_extra < 0)
        right_edges_extra = 0;    
    if(top_edges_extra < 0)
        top_edges_extra = 0;   
    if(bottom_edges_extra < 0)
        bottom_edges_extra = 0;
      
    copy_slice(net->layers[l-1].delta, net->layers[l-1].delta, net->batch, net->layers[l].c,
        net->layers[l-1].delta_in_h_with_boundry + top_edges_extra + bottom_edges_extra,
        net->layers[l-1].delta_in_w_with_boundry + left_edges_extra + right_edges_extra,
        net->layers[l-1].delta_in_h_with_boundry,
        net->layers[l-1].delta_in_w_with_boundry,
        left_edges_extra, top_edges_extra, 0, 0,
        net->layers[l-1].delta_in_h_with_boundry, net->layers[l-1].delta_in_w_with_boundry,
        net->layers[l-1].delta_in_h_with_boundry, net->layers[l-1].delta_in_w_with_boundry,
        net->workspace);    
}

void assemble_tile(network* net, int batch, int depth,
                   float* target, float* core_tile_data,
                   int core_tile_height, int core_tile_width,
                   int left_boundry_edges, int right_boundry_edges, int top_boundry_edges, int bottom_boundry_edges,
                   int device_id_x, int device_id_y, int NUM_TILES_X, int NUM_TILES_Y){

    int full_height = core_tile_height + top_boundry_edges + bottom_boundry_edges;
    int full_width = core_tile_width + left_boundry_edges + right_boundry_edges;

    float* core_tile_temp = calloc((batch*depth*core_tile_height*core_tile_width), sizeof(float));
    memcpy(core_tile_temp, core_tile_data, batch*depth*core_tile_height*core_tile_width*sizeof(float));

    copy_slice(target, core_tile_data, batch, depth,
        core_tile_height, core_tile_width, full_height, full_width,
        0, 0, left_boundry_edges, top_boundry_edges,
        core_tile_height, core_tile_width, core_tile_height, core_tile_width,
        net->workspace);
    
    float* transmit_data;

    // //Top left
    if((top_boundry_edges > 0) && (left_boundry_edges > 0)){

        float* boundry_top_left;

        get_group_boundry_data_device(
            NUM_TILES_X, NUM_TILES_Y,
            &boundry_top_left, 
            top_boundry_edges, left_boundry_edges, depth, batch,
            BOTTOM_RIGHT, 
            device_id_x-1, device_id_y-1,
            device_id_x, device_id_y);

        copy_slice(target, boundry_top_left, batch, depth,
            top_boundry_edges, left_boundry_edges, full_height, full_width,
            0, 0, 0, 0,
            top_boundry_edges, left_boundry_edges, top_boundry_edges, left_boundry_edges,
            net->workspace);

        free(boundry_top_left);

        //SEND TOP LEFT
        if((device_id_y > 0) && (device_id_x > 0)){
            int rows = bottom_boundry_edges;
            int cols = right_boundry_edges;
            transmit_data = calloc((batch*depth*rows*cols), sizeof(float));

            copy_slice(transmit_data, core_tile_temp, batch, depth,
                core_tile_height, core_tile_width, rows, cols,
                0, 0, 0, 0,
                rows, cols, rows, cols,
                net->workspace);

    
            send_boundry(transmit_data, batch*depth*rows*cols, device_id_x-1, device_id_y-1);
                
            free(transmit_data);

        }
    }


    //Top
    if(top_boundry_edges > 0){
        //receive top edges
        float* boundry_top;

        get_group_boundry_data_device(
            NUM_TILES_X, NUM_TILES_Y,
            &boundry_top, 
            top_boundry_edges, core_tile_width, depth, batch,
            BOTTOM, 
            device_id_x, device_id_y-1,
            device_id_x, device_id_y);

        copy_slice(target, boundry_top, batch, depth,
            top_boundry_edges, core_tile_width, full_height, full_width,
            0, 0, left_boundry_edges, 0,
            top_boundry_edges, core_tile_width, top_boundry_edges, core_tile_width,
            net->workspace);

        free(boundry_top);

        //SEND TOP
        if(device_id_y > 0){
            int rows = bottom_boundry_edges;
            int cols = core_tile_width;
            transmit_data = calloc((batch*depth*rows*cols), sizeof(float));

            copy_slice(transmit_data, core_tile_temp, batch, depth,
                core_tile_height, core_tile_width, rows, cols,
                0, 0, 0, 0,
                rows, cols, rows, cols,
                net->workspace);
            
            send_boundry(transmit_data, batch*depth*rows*cols, device_id_x, device_id_y-1);
            
            free(transmit_data);
        }
    } 



    //Top Right
    if(top_boundry_edges > 0 && right_boundry_edges > 0){
        //receive top-right edges
        float* boundry_top_right;

        get_group_boundry_data_device(
            NUM_TILES_X, NUM_TILES_Y,
            &boundry_top_right, 
            top_boundry_edges, right_boundry_edges, depth, batch,
            BOTTOM_LEFT, 
            device_id_x+1, device_id_y-1,
            device_id_x, device_id_y);

        copy_slice(target, boundry_top_right, batch, depth,
            top_boundry_edges, right_boundry_edges, full_height, full_width,
            0, 0, left_boundry_edges + core_tile_width, 0,
            top_boundry_edges, right_boundry_edges, top_boundry_edges, right_boundry_edges,
            net->workspace);

        free(boundry_top_right);

        //SEND TOP-RIGHT
        if((device_id_y > 0) && (device_id_x < (NUM_TILES_X-1))){
            int rows = bottom_boundry_edges;
            int cols = left_boundry_edges;
            transmit_data = calloc((batch*depth*rows*cols), sizeof(float));

            copy_slice(transmit_data, core_tile_temp, batch, depth,
                core_tile_height, core_tile_width, rows, cols,
                core_tile_width - right_boundry_edges, 0, 0, 0,
                rows, cols, rows, cols,
                net->workspace);
            
            send_boundry(transmit_data, batch*depth*rows*cols, device_id_x+1, device_id_y-1);
            
            free(transmit_data);
        }
    } 

    //LEFT
    if(left_boundry_edges > 0){
        //receive Left edges
        float* boundry_left;

        get_group_boundry_data_device(
            NUM_TILES_X, NUM_TILES_Y,
            &boundry_left, 
            core_tile_height, left_boundry_edges, depth, batch,
            RIGHT, 
            device_id_x-1, device_id_y,
            device_id_x, device_id_y);

        copy_slice(target, boundry_left, batch, depth,
            core_tile_height, left_boundry_edges, full_height, full_width,
            0, 0, 0, top_boundry_edges,
            core_tile_height, left_boundry_edges, core_tile_height, left_boundry_edges,
            net->workspace);

        free(boundry_left);

        //SEND Left
        if(device_id_x > 0){
            int rows = core_tile_height;
            int cols = right_boundry_edges;
            transmit_data = calloc((batch*depth*rows*cols), sizeof(float));

            copy_slice(transmit_data, core_tile_temp, batch, depth,
                core_tile_height, core_tile_width, rows, cols,
                0, 0, 0, 0,
                rows, cols, rows, cols,
                net->workspace);
            
            send_boundry(transmit_data, batch*depth*rows*cols, device_id_x - 1, device_id_y);
            
            free(transmit_data);
        }
    } 

    //RIGHT
    if(right_boundry_edges > 0){
        //SEND Right
        if(device_id_x < (NUM_TILES_X-1)){
            int rows = core_tile_height;
            int cols = left_boundry_edges;
            transmit_data = calloc((batch*depth*rows*cols), sizeof(float));

            copy_slice(transmit_data, core_tile_temp, batch, depth,
                core_tile_height, core_tile_width, rows, cols,
                core_tile_width - right_boundry_edges, 0, 0, 0,
                rows, cols, rows, cols,
                net->workspace);
            
            send_boundry(transmit_data, batch*depth*rows*cols, device_id_x+1, device_id_y);
            
            free(transmit_data);
        }

        //receive Right edges
        float* boundry_right;

        get_group_boundry_data_device(
            NUM_TILES_X, NUM_TILES_Y,
            &boundry_right, 
            core_tile_height, right_boundry_edges, depth, batch,
            LEFT, 
            device_id_x+1, device_id_y,
            device_id_x, device_id_y);

        copy_slice(target, boundry_right, batch, depth,
            core_tile_height, right_boundry_edges, full_height, full_width,
            0, 0, left_boundry_edges + core_tile_width, top_boundry_edges,
            core_tile_height, right_boundry_edges, core_tile_height, right_boundry_edges,
            net->workspace);

        free(boundry_right);
    }

    //BOTTOM LEFT
    if((bottom_boundry_edges > 0) && (left_boundry_edges > 0)){
        //SEND Bottom-Left
        if((device_id_y < (NUM_TILES_Y-1)) && (device_id_x > 0)){
            int rows = top_boundry_edges;
            int cols = right_boundry_edges;
            transmit_data = calloc((batch*depth*rows*cols), sizeof(float));

            copy_slice(transmit_data, core_tile_temp, batch, depth,
                core_tile_height, core_tile_width, rows, cols,
                0, core_tile_height - top_boundry_edges, 0, 0,
                rows, cols, rows, cols,
                net->workspace);
            
            send_boundry(transmit_data, batch*depth*rows*cols, device_id_x-1, device_id_y+1);
            
            free(transmit_data);
        }

        //receive Bottom-Left edges
        float* boundry_bottom_left;

        get_group_boundry_data_device(
            NUM_TILES_X, NUM_TILES_Y,
            &boundry_bottom_left, 
            bottom_boundry_edges, left_boundry_edges, depth, batch,
            TOP_RIGHT, 
            device_id_x-1, device_id_y+1,
            device_id_x, device_id_y);

        copy_slice(target, boundry_bottom_left, batch, depth,
            bottom_boundry_edges, left_boundry_edges, full_height, full_width,
            0, 0, 0, top_boundry_edges + core_tile_height,
            bottom_boundry_edges, left_boundry_edges, bottom_boundry_edges, left_boundry_edges,
            net->workspace);

        free(boundry_bottom_left);
    }

    //BOTTOM
    if(bottom_boundry_edges > 0){
        //SEND Bottom
        if(device_id_y < (NUM_TILES_Y-1)){
            int rows = top_boundry_edges;
            int cols = core_tile_width;
            transmit_data = calloc((batch*depth*rows*cols), sizeof(float));

            copy_slice(transmit_data, core_tile_temp, batch, depth,
                core_tile_height, core_tile_width, rows, cols,
                0, core_tile_height - top_boundry_edges, 0, 0,
                rows, cols, rows, cols,
                net->workspace);
            
            send_boundry(transmit_data, batch*depth*rows*cols, device_id_x, device_id_y+1);
            
            free(transmit_data);
        }

        //receive Bottom
        float* boundry_bottom;

        get_group_boundry_data_device(
            NUM_TILES_X, NUM_TILES_Y,
            &boundry_bottom, 
            bottom_boundry_edges, core_tile_width, depth, batch,
            TOP, 
            device_id_x, device_id_y+1,
            device_id_x, device_id_y);

        copy_slice(target, boundry_bottom, batch, depth,
            bottom_boundry_edges, core_tile_width, full_height, full_width,
            0, 0, left_boundry_edges, top_boundry_edges + core_tile_height,
            bottom_boundry_edges, core_tile_width, bottom_boundry_edges, core_tile_width,
            net->workspace);

        free(boundry_bottom);
    }


    //BOTTOM RIGHT
    if(bottom_boundry_edges > 0 && right_boundry_edges > 0){
        //SEND Bottom-Right
        if((device_id_y < (NUM_TILES_Y-1)) && (device_id_x < (NUM_TILES_X-1))){
            int rows = top_boundry_edges;
            int cols = left_boundry_edges;
            transmit_data = calloc((batch*depth*rows*cols), sizeof(float));

            copy_slice(transmit_data, core_tile_temp, batch, depth,
                core_tile_height, core_tile_width, rows, cols,
                core_tile_width - left_boundry_edges, core_tile_height - top_boundry_edges, 0, 0,
                rows, cols, rows, cols,
                net->workspace);
            
            send_boundry(transmit_data, batch*depth*rows*cols, device_id_x+1, device_id_y+1);
            
            free(transmit_data);
        }

        //receive Bottom-Right
        float* boundry_bottom_right;

        get_group_boundry_data_device(
            NUM_TILES_X, NUM_TILES_Y,
            &boundry_bottom_right, 
            bottom_boundry_edges, right_boundry_edges, depth, batch,
            TOP_LEFT, 
            device_id_x+1, device_id_y+1,
            device_id_x, device_id_y);

        copy_slice(target, boundry_bottom_right, batch, depth,
            bottom_boundry_edges, right_boundry_edges, full_height, full_width,
            0, 0, left_boundry_edges + core_tile_width, top_boundry_edges + core_tile_height,
            bottom_boundry_edges, right_boundry_edges, bottom_boundry_edges, right_boundry_edges,
            net->workspace);

        free(boundry_bottom_right);
    }

    free(core_tile_temp);

}

#ifdef GPU 

void assemble_tile_gpu(network* net, int batch, int depth,
                   float* target, float* core_tile_data,
                   int core_tile_height, int core_tile_width,
                   int left_boundry_edges, int right_boundry_edges, int top_boundry_edges, int bottom_boundry_edges,
                   int device_id_x, int device_id_y, int NUM_TILES_X, int NUM_TILES_Y){

    int full_height = core_tile_height + top_boundry_edges + bottom_boundry_edges;
    int full_width = core_tile_width + left_boundry_edges + right_boundry_edges;

    float* core_tile_temp;
    cudaError_t status = cudaMalloc((void **)&core_tile_temp, batch*depth*core_tile_height*core_tile_width*sizeof(float));
    check_error(status);
    status = cudaMemcpy(core_tile_temp, core_tile_data, batch*depth*core_tile_height*core_tile_width*sizeof(float), cudaMemcpyDeviceToDevice);
    check_error(status);

    copy_slice_gpu(target, core_tile_data, batch, depth,
        core_tile_height, core_tile_width, full_height, full_width,
        0, 0, left_boundry_edges, top_boundry_edges,
        core_tile_height, core_tile_width, core_tile_height, core_tile_width,
        net->workspace);

    float* transmit_data;
    float* transmit_data_gpu;

    // //Top left
    if((top_boundry_edges > 0) && (left_boundry_edges > 0)){

        float* boundry_top_left;

        get_group_boundry_data_device(
            NUM_TILES_X, NUM_TILES_Y,
            &boundry_top_left, 
            top_boundry_edges, left_boundry_edges, depth, batch,
            BOTTOM_RIGHT, 
            device_id_x-1, device_id_y-1,
            device_id_x, device_id_y);

        float * boundry_top_left_gpu = cuda_make_array(boundry_top_left, top_boundry_edges*left_boundry_edges*depth*batch);

        copy_slice_gpu(target, boundry_top_left_gpu, batch, depth,
            top_boundry_edges, left_boundry_edges, full_height, full_width,
            0, 0, 0, 0,
            top_boundry_edges, left_boundry_edges, top_boundry_edges, left_boundry_edges,
            net->workspace);

        cuda_free(boundry_top_left_gpu);
        free(boundry_top_left);

        //SEND TOP LEFT
        if((device_id_y > 0) && (device_id_x > 0)){
            int rows = bottom_boundry_edges;
            int cols = right_boundry_edges;
            transmit_data = calloc((batch*depth*rows*cols), sizeof(float));
            status = cudaMalloc((void **)&transmit_data_gpu, batch*depth*rows*cols*sizeof(float));

            copy_slice_gpu(transmit_data_gpu, core_tile_temp, batch, depth,
                core_tile_height, core_tile_width, rows, cols,
                0, 0, 0, 0,
                rows, cols, rows, cols,
                net->workspace);
            cuda_pull_array(transmit_data_gpu, transmit_data, batch*depth*rows*cols);
    
            send_boundry(transmit_data, batch*depth*rows*cols, device_id_x-1, device_id_y-1);
                
            free(transmit_data);
            cuda_free(transmit_data_gpu);

        }
    }

    //Top
    if(top_boundry_edges > 0){
        //receive top edges
        float* boundry_top;

        get_group_boundry_data_device(
            NUM_TILES_X, NUM_TILES_Y,
            &boundry_top, 
            top_boundry_edges, core_tile_width, depth, batch,
            BOTTOM, 
            device_id_x, device_id_y-1,
            device_id_x, device_id_y);

        float * boundry_top_gpu = cuda_make_array(boundry_top, top_boundry_edges*core_tile_width*depth*batch);

        copy_slice_gpu(target, boundry_top_gpu, batch, depth,
            top_boundry_edges, core_tile_width, full_height, full_width,
            0, 0, left_boundry_edges, 0,
            top_boundry_edges, core_tile_width, top_boundry_edges, core_tile_width,
            net->workspace);

        cuda_free(boundry_top_gpu);
        free(boundry_top);

        //SEND TOP
        if(device_id_y > 0){
            int rows = bottom_boundry_edges;
            int cols = core_tile_width;
            transmit_data = calloc((batch*depth*rows*cols), sizeof(float));
            status = cudaMalloc((void **)&transmit_data_gpu, batch*depth*rows*cols*sizeof(float));

            copy_slice_gpu(transmit_data_gpu, core_tile_temp, batch, depth,
                core_tile_height, core_tile_width, rows, cols,
                0, 0, 0, 0,
                rows, cols, rows, cols,
                net->workspace);

            cuda_pull_array(transmit_data_gpu, transmit_data, batch*depth*rows*cols);
            send_boundry(transmit_data, batch*depth*rows*cols, device_id_x, device_id_y-1);
            
            free(transmit_data);
            cuda_free(transmit_data_gpu);
        }
    } 


    //Top Right
    if(top_boundry_edges > 0 && right_boundry_edges > 0){
        //receive top-right edges
        float* boundry_top_right;

        get_group_boundry_data_device(
            NUM_TILES_X, NUM_TILES_Y,
            &boundry_top_right, 
            top_boundry_edges, right_boundry_edges, depth, batch,
            BOTTOM_LEFT, 
            device_id_x+1, device_id_y-1,
            device_id_x, device_id_y);
        
        float * boundry_top_right_gpu = cuda_make_array(boundry_top_right, top_boundry_edges*right_boundry_edges*depth*batch);

        copy_slice_gpu(target, boundry_top_right_gpu, batch, depth,
            top_boundry_edges, right_boundry_edges, full_height, full_width,
            0, 0, left_boundry_edges + core_tile_width, 0,
            top_boundry_edges, right_boundry_edges, top_boundry_edges, right_boundry_edges,
            net->workspace);

        cuda_free(boundry_top_right_gpu);
        free(boundry_top_right);

        //SEND TOP-RIGHT
        if((device_id_y > 0) && (device_id_x < (NUM_TILES_X-1))){
            int rows = bottom_boundry_edges;
            int cols = left_boundry_edges;
            transmit_data = calloc((batch*depth*rows*cols), sizeof(float));
            status = cudaMalloc((void **)&transmit_data_gpu, batch*depth*rows*cols*sizeof(float));

            copy_slice_gpu(transmit_data_gpu, core_tile_temp, batch, depth,
                core_tile_height, core_tile_width, rows, cols,
                core_tile_width - right_boundry_edges, 0, 0, 0,
                rows, cols, rows, cols,
                net->workspace);
            
            cuda_pull_array(transmit_data_gpu, transmit_data, batch*depth*rows*cols);
            send_boundry(transmit_data, batch*depth*rows*cols, device_id_x+1, device_id_y-1);
            
            free(transmit_data);
            cuda_free(transmit_data_gpu);
        }
    }

    //LEFT
    if(left_boundry_edges > 0){
        //receive Left edges
        float* boundry_left;

        get_group_boundry_data_device(
            NUM_TILES_X, NUM_TILES_Y,
            &boundry_left, 
            core_tile_height, left_boundry_edges, depth, batch,
            RIGHT, 
            device_id_x-1, device_id_y,
            device_id_x, device_id_y);
        
        float * boundry_left_gpu = cuda_make_array(boundry_left, core_tile_height*left_boundry_edges*depth*batch);

        copy_slice_gpu(target, boundry_left_gpu, batch, depth,
            core_tile_height, left_boundry_edges, full_height, full_width,
            0, 0, 0, top_boundry_edges,
            core_tile_height, left_boundry_edges, core_tile_height, left_boundry_edges,
            net->workspace);

        cuda_free(boundry_left_gpu);
        free(boundry_left);

        //SEND Left
        if(device_id_x > 0){
            int rows = core_tile_height;
            int cols = right_boundry_edges;
            transmit_data = calloc((batch*depth*rows*cols), sizeof(float));
            status = cudaMalloc((void **)&transmit_data_gpu, batch*depth*rows*cols*sizeof(float));

            copy_slice_gpu(transmit_data_gpu, core_tile_temp, batch, depth,
                core_tile_height, core_tile_width, rows, cols,
                0, 0, 0, 0,
                rows, cols, rows, cols,
                net->workspace);
            
            cuda_pull_array(transmit_data_gpu, transmit_data, batch*depth*rows*cols);
            send_boundry(transmit_data, batch*depth*rows*cols, device_id_x - 1, device_id_y);
            
            free(transmit_data);
            cuda_free(transmit_data_gpu);
        }
    }  

    //RIGHT
    if(right_boundry_edges > 0){
        //SEND Right
        if(device_id_x < (NUM_TILES_X-1)){
            int rows = core_tile_height;
            int cols = left_boundry_edges;
            transmit_data = calloc((batch*depth*rows*cols), sizeof(float));
            status = cudaMalloc((void **)&transmit_data_gpu, batch*depth*rows*cols*sizeof(float));

            copy_slice_gpu(transmit_data_gpu, core_tile_temp, batch, depth,
                core_tile_height, core_tile_width, rows, cols,
                core_tile_width - right_boundry_edges, 0, 0, 0,
                rows, cols, rows, cols,
                net->workspace);
            
            cuda_pull_array(transmit_data_gpu, transmit_data, batch*depth*rows*cols);
            send_boundry(transmit_data, batch*depth*rows*cols, device_id_x+1, device_id_y);
            
            free(transmit_data);
            cuda_free(transmit_data_gpu);
        }

        //receive Right edges
        float* boundry_right;

        get_group_boundry_data_device(
            NUM_TILES_X, NUM_TILES_Y,
            &boundry_right, 
            core_tile_height, right_boundry_edges, depth, batch,
            LEFT, 
            device_id_x+1, device_id_y,
            device_id_x, device_id_y);
        
        float * boundry_right_gpu = cuda_make_array(boundry_right, core_tile_height*right_boundry_edges*depth*batch);

        copy_slice_gpu(target, boundry_right_gpu, batch, depth,
            core_tile_height, right_boundry_edges, full_height, full_width,
            0, 0, left_boundry_edges + core_tile_width, top_boundry_edges,
            core_tile_height, right_boundry_edges, core_tile_height, right_boundry_edges,
            net->workspace);

        cuda_free(boundry_right_gpu);
        free(boundry_right);
    }

    //BOTTOM LEFT
    if((bottom_boundry_edges > 0) && (left_boundry_edges > 0)){
        //SEND Bottom-Left
        if((device_id_y < (NUM_TILES_Y-1)) && (device_id_x > 0)){
            int rows = top_boundry_edges;
            int cols = right_boundry_edges;
            transmit_data = calloc((batch*depth*rows*cols), sizeof(float));
            status = cudaMalloc((void **)&transmit_data_gpu, batch*depth*rows*cols*sizeof(float));

            copy_slice_gpu(transmit_data_gpu, core_tile_temp, batch, depth,
                core_tile_height, core_tile_width, rows, cols,
                0, core_tile_height - top_boundry_edges, 0, 0,
                rows, cols, rows, cols,
                net->workspace);
            
            cuda_pull_array(transmit_data_gpu, transmit_data, batch*depth*rows*cols);
            send_boundry(transmit_data, batch*depth*rows*cols, device_id_x-1, device_id_y+1);
            
            free(transmit_data);
            cuda_free(transmit_data_gpu);
        }

        //receive Bottom-Left edges
        float* boundry_bottom_left;

        get_group_boundry_data_device(
            NUM_TILES_X, NUM_TILES_Y,
            &boundry_bottom_left, 
            bottom_boundry_edges, left_boundry_edges, depth, batch,
            TOP_RIGHT, 
            device_id_x-1, device_id_y+1,
            device_id_x, device_id_y);

        float * boundry_bottom_left_gpu = cuda_make_array(boundry_bottom_left, bottom_boundry_edges*left_boundry_edges*depth*batch);

        copy_slice_gpu(target, boundry_bottom_left_gpu, batch, depth,
            bottom_boundry_edges, left_boundry_edges, full_height, full_width,
            0, 0, 0, top_boundry_edges + core_tile_height,
            bottom_boundry_edges, left_boundry_edges, bottom_boundry_edges, left_boundry_edges,
            net->workspace);

        cuda_free(boundry_bottom_left_gpu);
        free(boundry_bottom_left);
    }

    //BOTTOM
    if(bottom_boundry_edges > 0){
        //SEND Bottom
        if(device_id_y < (NUM_TILES_Y-1)){
            int rows = top_boundry_edges;
            int cols = core_tile_width;
            transmit_data = calloc((batch*depth*rows*cols), sizeof(float));
            status = cudaMalloc((void **)&transmit_data_gpu, batch*depth*rows*cols*sizeof(float));

            copy_slice_gpu(transmit_data_gpu, core_tile_temp, batch, depth,
                core_tile_height, core_tile_width, rows, cols,
                0, core_tile_height - top_boundry_edges, 0, 0,
                rows, cols, rows, cols,
                net->workspace);
            
            cuda_pull_array(transmit_data_gpu, transmit_data, batch*depth*rows*cols);
            send_boundry(transmit_data, batch*depth*rows*cols, device_id_x, device_id_y+1);
            
            free(transmit_data);
            cuda_free(transmit_data_gpu);
        }

        //receive Bottom
        float* boundry_bottom;

        get_group_boundry_data_device(
            NUM_TILES_X, NUM_TILES_Y,
            &boundry_bottom, 
            bottom_boundry_edges, core_tile_width, depth, batch,
            TOP, 
            device_id_x, device_id_y+1,
            device_id_x, device_id_y);
        
        float * boundry_bottom_gpu = cuda_make_array(boundry_bottom, bottom_boundry_edges*core_tile_width*depth*batch);

        copy_slice_gpu(target, boundry_bottom_gpu, batch, depth,
            bottom_boundry_edges, core_tile_width, full_height, full_width,
            0, 0, left_boundry_edges, top_boundry_edges + core_tile_height,
            bottom_boundry_edges, core_tile_width, bottom_boundry_edges, core_tile_width,
            net->workspace);

        cuda_free(boundry_bottom_gpu);
        free(boundry_bottom);
    }


    //BOTTOM RIGHT
    if(bottom_boundry_edges > 0 && right_boundry_edges > 0){
        //SEND Bottom-Right
        if((device_id_y < (NUM_TILES_Y-1)) && (device_id_x < (NUM_TILES_X-1))){
            int rows = top_boundry_edges;
            int cols = left_boundry_edges;
            transmit_data = calloc((batch*depth*rows*cols), sizeof(float));
            status = cudaMalloc((void **)&transmit_data_gpu, batch*depth*rows*cols*sizeof(float));

            copy_slice_gpu(transmit_data_gpu, core_tile_temp, batch, depth,
                core_tile_height, core_tile_width, rows, cols,
                core_tile_width - left_boundry_edges, core_tile_height - top_boundry_edges, 0, 0,
                rows, cols, rows, cols,
                net->workspace);
            
            cuda_pull_array(transmit_data_gpu, transmit_data, batch*depth*rows*cols);
            send_boundry(transmit_data, batch*depth*rows*cols, device_id_x+1, device_id_y+1);
            
            free(transmit_data);
            cuda_free(transmit_data_gpu);
        }

        //receive Bottom-Right
        float* boundry_bottom_right;

        get_group_boundry_data_device(
            NUM_TILES_X, NUM_TILES_Y,
            &boundry_bottom_right, 
            bottom_boundry_edges, right_boundry_edges, depth, batch,
            TOP_LEFT, 
            device_id_x+1, device_id_y+1,
            device_id_x, device_id_y);

        float * boundry_bottom_right_gpu = cuda_make_array(boundry_bottom_right, bottom_boundry_edges*right_boundry_edges*depth*batch);

        copy_slice_gpu(target, boundry_bottom_right_gpu, batch, depth,
            bottom_boundry_edges, right_boundry_edges, full_height, full_width,
            0, 0, left_boundry_edges + core_tile_width, top_boundry_edges + core_tile_height,
            bottom_boundry_edges, right_boundry_edges, bottom_boundry_edges, right_boundry_edges,
            net->workspace);

        cuda_free(boundry_bottom_right_gpu);
        free(boundry_bottom_right);
    }

}

#endif

void copy_slice(float* dst, float* src, int batch, int depth,
                int height_src, int width_src, int height_dst, int width_dst,
                int src_start_x, int src_start_y, int dst_start_x, int dst_start_y,
                int copy_height_src, int copy_width_src, int copy_height_dst, int copy_width_dst,
                float* workspace){

    float* src_intermediate = src;

    //if(dst == src){
      //  src_intermediate = workspace;
    
        for(int b = 0; b < batch; b++){
            for(int d = 0; d < depth; d++){
                for(int h = 0; h < copy_height_src; h++){
                    for(int w = 0; w < copy_width_src; w++){
                        workspace[b*depth*copy_height_src*copy_width_src + d*copy_height_src*copy_width_src + h*copy_width_src + w] = 
                        src[b*depth*height_src*width_src + d*height_src*width_src + (h + src_start_y)*width_src + w + src_start_x];
                    }   
                }       
            }        
        }
    //}

    for(int b = 0; b < batch; b++){
        for(int d = 0; d < depth; d++){
            for(int h = 0; h < copy_height_dst; h++){
                for(int w = 0; w < copy_width_dst; w++){
                    dst[b*depth*height_dst*width_dst + d*height_dst*width_dst + (h + dst_start_y)*width_dst + w + dst_start_x] =
                    workspace[b*depth*copy_height_dst*copy_width_dst + d*copy_height_dst*copy_width_dst + h*copy_width_dst + w];
                }   
            }       
        }        
    }
}