#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>
#include <stdio.h>
#include <stdlib.h>

#define IS_INTERLACED(a) ((a)&MB_TYPE_INTERLACED)
#define IS_16X16(a)      ((a)&MB_TYPE_16x16)
#define IS_16X8(a)       ((a)&MB_TYPE_16x8)
#define IS_8X16(a)       ((a)&MB_TYPE_8x16)
#define IS_8X8(a)        ((a)&MB_TYPE_8x8)
#define USES_LIST(a, list) ((a) & ((MB_TYPE_P0L0|MB_TYPE_P1L0)<<(2*(list))))


bool GetNextFrame(AVFormatContext *pFormatCtx, AVCodecContext *pCodecCtx, 
    int videoStream, AVFrame *pFrame)
{
    static AVPacket packet;
    static int      bytesRemaining = 0;
    static uint8_t  *rawData;
    static bool     fFirstTime = true;
    int             bytesDecoded;
    int             frameFinished;

    // First time we're called, set packet.data to NULL to indicate it
    // doesn't have to be freed
    if (fFirstTime) {
        fFirstTime = false;
        packet.data = NULL;
    }

    // Decode packets until we have decoded a complete frame
    while (true) {
        // Work on the current packet until we have decoded all of it
        while (bytesRemaining > 0) {
            // Decode the next chunk of data
            bytesDecoded = avcodec_decode_video(pCodecCtx, pFrame,
                &frameFinished, rawData, bytesRemaining);

            // Was there an error?
            if (bytesDecoded < 0) {
                fprintf(stderr, "Error while decoding frame\n");
                return false;
            }

            bytesRemaining -= bytesDecoded;
            rawData += bytesDecoded;

            // Did we finish the current frame? Then we can return
            if (frameFinished)
                return true;
        }

        // Read the next packet, skipping all packets that aren't for this
        // stream
        do {
            // Free old packet
            if (packet.data != NULL)
                av_free_packet(&packet);

            // Read new packet
            if (av_read_packet(pFormatCtx, &packet) < 0)
                goto loop_exit;
        } while (packet.stream_index != videoStream);

        bytesRemaining = packet.size;
        rawData = packet.data;
    }

loop_exit:

    // Decode the rest of the last frame
    bytesDecoded = avcodec_decode_video(pCodecCtx, pFrame, &frameFinished, 
        rawData, bytesRemaining);

    // Free last packet
    if (packet.data != NULL)
        av_free_packet(&packet);

    fprintf(stderr, "finished: %d\n", frameFinished);
    return frameFinished != 0;
}

void print_vector(int x, int y, int dx, int dy)
{
    printf("%d %d ; %d %d\n", x, y, dx, dy);
}

/* Print motion vector for each macroblock in this frame.  If there is
 * no motion vector in some macroblock, it prints a magic number NO_MV. */
void printMVMatrix(int index, AVFrame *pict, AVCodecContext *ctx)
{
    const int mb_width  = (ctx->width + 15) / 16;
    const int mb_height = (ctx->height + 15) / 16;
    const int mb_stride = mb_width + 1;
    const int mv_sample_log2 = 4 - pict->motion_subsample_log2;
    const int mv_stride = (mb_width << mv_sample_log2) + (ctx->codec_id == CODEC_ID_H264 ? 0 : 1);
    const int quarter_sample = (ctx->flags & CODEC_FLAG_QPEL) != 0;
    const int shift = 1 + quarter_sample;


    printf("frame %d, %d x %d\n", index, mb_height, mb_width);

    for (int mb_y = 0; mb_y < mb_height; mb_y++) {
	for (int mb_x = 0; mb_x < mb_width; mb_x++) {
	    const int mb_index = mb_x + mb_y * mb_stride;
	    if (pict->motion_val) {
		for (int type = 0; type < 3; type++) {
		    int direction = 0;
		    switch (type) {
			case 0:
			    if (pict->pict_type != FF_P_TYPE)
				continue;
			    direction = 0;
			    break;
			case 1:
			    if (pict->pict_type != FF_B_TYPE)
			       	continue;
			    direction = 0;
			    break;
			case 2:
			    if (pict->pict_type != FF_B_TYPE)
				continue;
			    direction = 1;
			    break;
		    }

		    if (!USES_LIST(pict->mb_type[mb_index], direction)) {
#define NO_MV 10000
			if (IS_8X8(pict->mb_type[mb_index])) {
			    print_vector(mb_x, mb_y, NO_MV, NO_MV);
			    print_vector(mb_x, mb_y, NO_MV, NO_MV);
			    print_vector(mb_x, mb_y, NO_MV, NO_MV);
			    print_vector(mb_x, mb_y, NO_MV, NO_MV);
			} else if (IS_16X8(pict->mb_type[mb_index])) {
			    print_vector(mb_x, mb_y, NO_MV, NO_MV);
			    print_vector(mb_x, mb_y, NO_MV, NO_MV);
			} else if (IS_8X16(pict->mb_type[mb_index])) {
			    print_vector(mb_x, mb_y, NO_MV, NO_MV);
			    print_vector(mb_x, mb_y, NO_MV, NO_MV);
			} else {
			    print_vector(mb_x, mb_y, NO_MV, NO_MV);
			}
#undef NO_MV
			continue;
		    }

		    if (IS_8X8(pict->mb_type[mb_index])) {
			for (int i = 0; i < 4; i++) {
			    int xy = (mb_x*2 + (i&1) + (mb_y*2 + (i>>1))*mv_stride) << (mv_sample_log2-1);
			    int dx = (pict->motion_val[direction][xy][0]>>shift);
			    int dy = (pict->motion_val[direction][xy][1]>>shift);
			    print_vector(mb_x, mb_y, dx, dy);
			}
		    } else if (IS_16X8(pict->mb_type[mb_index])) {
			for (int i = 0; i < 2; i++) {
			    int xy = (mb_x*2 + (mb_y*2 + i)*mv_stride) << (mv_sample_log2-1);
			    int dx = (pict->motion_val[direction][xy][0]>>shift);
			    int dy = (pict->motion_val[direction][xy][1]>>shift);

			    if (IS_INTERLACED(pict->mb_type[mb_index]))
				dy *= 2;

			    print_vector(mb_x, mb_y, dx, dy);
			}
		    } else if (IS_8X16(pict->mb_type[mb_index])) {
			for (int i = 0; i < 2; i++) {
			    int xy =  (mb_x*2 + i + mb_y*2*mv_stride) << (mv_sample_log2-1);
			    int dx = (pict->motion_val[direction][xy][0]>>shift);
			    int dy = (pict->motion_val[direction][xy][1]>>shift);

			    if (IS_INTERLACED(pict->mb_type[mb_index]))
				dy *= 2;

			    print_vector(mb_x, mb_y, dx, dy);
			}
		    } else {
			int xy = (mb_x + mb_y*mv_stride) << mv_sample_log2;
			int dx = (pict->motion_val[direction][xy][0]>>shift);
			int dy = (pict->motion_val[direction][xy][1]>>shift);
			print_vector(mb_x, mb_y, dx, dy);
		    }
		}
	    }
	    printf("--\n");
	}
	printf("====\n");
    }
}

int main(int argc, char *argv[])
{
    AVFormatContext *pFormatCtx;
    AVCodecContext  *pCodecCtx;
    AVCodec         *pCodec;
    AVFrame         *pFrame; 
    int             videoStream;

    // Register all formats and codecs
    av_register_all();

    // Open video file
    if (av_open_input_file(&pFormatCtx, argv[1], NULL, 0, NULL) != 0)
        return -1; // Couldn't open file

    // Retrieve stream information
    if (av_find_stream_info(pFormatCtx) < 0)
        return -1; // Couldn't find stream information

    // Dump information about file onto standard error
//    dump_format(pFormatCtx, 0, argv[1], false);

    // Find the first video stream
    videoStream = -1;
    for (int i = 0; i < pFormatCtx->nb_streams; i++) {
	AVCodecContext *cc = pFormatCtx->streams[i]->codec;
        if (cc->codec_type==CODEC_TYPE_VIDEO) {
	    // don't care FF_DEBUG_VIS_MV_B_BACK
	    cc->debug_mv = FF_DEBUG_VIS_MV_P_FOR | FF_DEBUG_VIS_MV_B_FOR;
            videoStream = i;
            break;
        }
    }
    if (videoStream == -1)
        return -1; // Didn't find a video stream

    // Get a pointer to the codec context for the video stream
    pCodecCtx = pFormatCtx->streams[videoStream]->codec;

    // Find the decoder for the video stream
    pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if (pCodec == NULL)
        return -1; // Codec not found

    // Inform the codec that we can handle truncated bitstreams -- i.e.,
    // bitstreams where frame boundaries can fall in the middle of packets
    if (pCodec->capabilities & CODEC_CAP_TRUNCATED)
        pCodecCtx->flags |= CODEC_FLAG_TRUNCATED;

    // Open codec
    if (avcodec_open(pCodecCtx, pCodec)<0)
        return -1; // Could not open codec

    // Hack to correct wrong frame rates that seem to be generated by some 
    // codecs
//    if (pCodecCtx->frame_rate>1000 && pCodecCtx->frame_rate_base==1)
//        pCodecCtx->frame_rate_base=1000;

    // Allocate video frame
    pFrame = avcodec_alloc_frame();


    int f = 1;

    while  (GetNextFrame(pFormatCtx, pCodecCtx, videoStream, pFrame)) {

	// Ignore I-Frame
	if (pFrame->pict_type != FF_I_TYPE)
	    printMVMatrix(f, pFrame, pCodecCtx);

	++f;
    }

    av_free(pFrame);
    avcodec_close(pCodecCtx);
    av_close_input_file(pFormatCtx);

    return 0;
}
