#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <poll.h>
#include <alsa/asoundlib.h>

#include <stk/Noise.h>
#include <stk/Iir.h>

#define PLAYBACK_DEVICE "default"
#define CAPTURE_DEVICE "default"

#define PCM_DEVICE "default"
#define BUF_SIZE 2048

#define DEBUG

#ifdef DEBUG
#define D(fmt, args...) fprintf(stderr, fmt, ##args)
#else
#define D(fmt, args...)
#endif

using namespace stk;

// g++ MySynth.cpp -o MySynth.o -lasound
snd_pcm_t *capture_handle;
snd_pcm_t *playback_handle;

struct audio_stream {
	snd_pcm_hw_params_t *hw_playback_params;
	snd_pcm_sw_params_t *sw_playback_params;
	snd_pcm_hw_params_t *hw_capture_params;
	snd_pcm_sw_params_t *sw_capture_params;
	unsigned int sample_rate;

	void *buffer;
	snd_pcm_uframes_t frame_size;
	unsigned int buffer_size;
	snd_pcm_format_t format;
	int channels;
};

void applyEffect(struct audio_stream *stream)
{
	short *buf = (short *)stream->buffer;

	//init StkFrames and convert buf to fit in range -1.0 to 1.0
	stk::StkFrames output(stream->frame_size, 1 );
	for (int i=0; i < stream->frame_size; i++){
		output[i] = static_cast<double>(buf[i])/0x8000;
	}

	//do filtering
	std::vector<stk::StkFloat> numerator( 5, 0.1 ); // create and initialize numerator coefficients
	std::vector<stk::StkFloat> denominator;         // create empty denominator coefficients
	denominator.push_back( 1.0 );              // populate our denomintor values
	denominator.push_back( 0.3 );
	denominator.push_back( -0.5 );
	stk::Iir filter( numerator, denominator );
	filter.tick( output );

	// fill buffer with filtered values
	for (int i=0; i < stream->frame_size; i++){
		buf[i]=static_cast<short>(output[i]*0x8000);
	}

}

int playback_callback(snd_pcm_sframes_t nframes, short buf[]) {
	int err;

	//printf("playback callback called with %lu frames\n", nframes);

	err = snd_pcm_writei(playback_handle, buf, nframes);
	if (err < 0) {
		fprintf(stderr, "write failed (%s)\n", snd_strerror(err));
	}

	return err;
}

int capture_callback(snd_pcm_sframes_t nframes, short buf[]) {

	int err;

	//printf("capture callback called with %lu frames\n", nframes);

	/* ... fill buf with data ... */
	err = snd_pcm_readi(capture_handle, buf, nframes);
	if (err != nframes)	{
		fprintf(stderr, "read failed (%s)\n", snd_strerror(err));
	}

	return err;
}

int open_and_init(struct audio_stream *stream)
{
	int err;
	snd_pcm_uframes_t val;

	// open playback device
	err = snd_pcm_open(&playback_handle, PLAYBACK_DEVICE, SND_PCM_STREAM_PLAYBACK, 0);
	if (err < 0) {
		fprintf(stderr, "cannot open output audio device %s (%s)\n",
				PLAYBACK_DEVICE,
				snd_strerror(err));
		exit(1);
	}
	// open capture device
	err = snd_pcm_open(&capture_handle, CAPTURE_DEVICE, SND_PCM_STREAM_CAPTURE, 0);
	if (err < 0) {
		fprintf(stderr, "cannot open input audio device %s (%s)\n",
				CAPTURE_DEVICE,
				snd_strerror(err));
		exit(1);
	}

	//allocate hw params
	err = snd_pcm_hw_params_malloc(&stream->hw_playback_params);
	if (err < 0) {
		fprintf(stderr, "cannot allocate playback hardware parameter structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_malloc(&stream->hw_capture_params);
	if (err < 0) {
		fprintf(stderr, "cannot allocate capture hardware parameter structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	// init hw_param structures
	err = snd_pcm_hw_params_any(playback_handle, stream->hw_playback_params);
	if (err < 0) {
		fprintf(stderr, "cannot initialize playback hardware parameter structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_any(capture_handle, stream->hw_capture_params);
	if (err < 0) {
		fprintf(stderr, "cannot initialize capture hardware parameter structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	//set access type
	err = snd_pcm_hw_params_set_access(playback_handle, stream->hw_playback_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0) {
		fprintf(stderr, "cannot set access type (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_set_access(capture_handle, stream->hw_capture_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0) {
		fprintf(stderr, "cannot set access type (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	//set sample formats
	err = snd_pcm_hw_params_set_format(playback_handle, stream->hw_playback_params, stream->format);
	if (err < 0) {
		fprintf(stderr, "cannot set sample format (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_set_format(capture_handle, stream->hw_capture_params, stream->format);
	if (err < 0) {
		fprintf(stderr, "cannot set sample format (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	//set sample rate
	err = snd_pcm_hw_params_set_rate_near(playback_handle, stream->hw_playback_params, &stream->sample_rate, 0);
	if (err < 0) {
		fprintf(stderr, "cannot set sample rate (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_set_rate_near(capture_handle, stream->hw_capture_params, &stream->sample_rate, 0);
	if (err < 0) {
		fprintf(stderr, "cannot set sample rate (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	// set chanel count
	err = snd_pcm_hw_params_set_channels(playback_handle, stream->hw_playback_params, stream->channels);
	if (err < 0) {
		fprintf(stderr, "cannot set channel count (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_set_channels(capture_handle, stream->hw_capture_params, stream->channels);
	if (err < 0) {
		fprintf(stderr, "cannot set channel count (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	// set parameters
	err = snd_pcm_hw_params(playback_handle, stream->hw_playback_params);
	if (err < 0) {
		fprintf(stderr, "cannot set parameters (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params(capture_handle, stream->hw_capture_params);
	if (err < 0) {
		fprintf(stderr, "cannot set parameters (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	//free params
	snd_pcm_hw_params_free(stream->hw_playback_params);
	snd_pcm_hw_params_free(stream->hw_capture_params);

	/* tell ALSA to wake us up whenever 4096 or more frames
	   of playback data can be delivered. Also, tell
	   ALSA that we'll start the device ourselves.
	 */

	err = snd_pcm_sw_params_malloc(&stream->sw_playback_params);
	if (err < 0) {
		fprintf(stderr, "cannot allocate software parameters structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_sw_params_current(playback_handle, stream->sw_playback_params);
	if (err < 0) {
		fprintf(stderr, "cannot initialize software parameters structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	snd_pcm_hw_params_get_period_size(stream->hw_playback_params, &val, NULL);

	fprintf(stderr, "period_size: %lu\n", val);

	stream->frame_size = val;
	stream->buffer_size = stream->frame_size * (snd_pcm_format_width(stream->format) / 8) * stream->channels;

	D("buffer_size: %u, frame_size %lu\n", stream->buffer_size, stream->frame_size);

	stream->buffer = malloc(stream->buffer_size);
	if (!stream->buffer) {
		perror("malloc():");
		exit(1);
	}

	err = snd_pcm_sw_params_set_avail_min(playback_handle, stream->sw_playback_params, val);
	if (err < 0) {
		fprintf(stderr, "cannot set minimum available count (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	//playback device will start to play when 2*BUF_SIZE of frames is available in its internal buffer
	//increase the latency, but be sure that underflow will not happpen.
	err = snd_pcm_sw_params_set_start_threshold(playback_handle, stream->sw_playback_params, stream->frame_size * 2);
	if (err < 0) {
		fprintf(stderr, "cannot set start mode (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_sw_params(playback_handle, stream->sw_playback_params);
	if (err < 0) {
		fprintf(stderr, "cannot set software parameters (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	/* the interface will interrupt the kernel every 4096 frames, and ALSA
	   will wake up this program very soon after that.
	 */

	err = snd_pcm_prepare(playback_handle);
	if (err < 0) {
		fprintf(stderr, "cannot prepare audio interface for use (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_prepare(capture_handle);
	if (err < 0) {
		fprintf(stderr, "cannot prepare audio interface for use (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	return 0;
}

int main(int argc, char *argv[]) {

	struct audio_stream stream;
	int err;
	int frames_played;
	int frames_captured;
	snd_pcm_uframes_t val;

	stream.format = SND_PCM_FORMAT_S16_LE;
	stream.sample_rate = (unsigned int)44100; // set sample rate
	stream.channels = 1;

	open_and_init(&stream);

	while (1) {

		/* wait till the capture device is ready for data, or 1 second
		 * has elapsed.
		 */

		if ((err = snd_pcm_wait(capture_handle, 1000)) < 0) {
			fprintf(stderr, "poll failed (%s)\n", strerror(errno));
			break;
		}

		// capture data
		frames_captured = capture_callback(stream.frame_size, (short *)stream.buffer);
		if (frames_captured != stream.frame_size) {
			fprintf(stderr, "capture callback failed\n");
			break;
		}

		//apply dummy filter
		applyEffect(&stream);

		/*
		// prpare playback device
		err = snd_pcm_prepare (playback_handle);
		if (err < 0) {
		fprintf (stderr, "cannot prepare playback interface for use (%s)\n",
		snd_strerror (err));
		exit (1);
		}
		 */

		/* wait till the playback device is ready for data, or 1 second
		 * has elapsed.
		 */

		if ((err = snd_pcm_wait(playback_handle, 1000)) < 0) {
			fprintf(stderr, "poll failed (%s)\n", strerror(errno));
			break;
		}

		//frames in buffer
		snd_pcm_status_t *playback_status;
		snd_pcm_status_alloca(&playback_status);
		snd_pcm_status(playback_handle, playback_status);	
		val = snd_pcm_status_get_avail_max(playback_status) - snd_pcm_avail_update(playback_handle);		
		D("frames available: %lu\n", val);

		/* deliver the data */
		frames_played = playback_callback(stream.frame_size, (short *)stream.buffer);
		if (frames_played != stream.frame_size) {
			fprintf(stderr, "playback callback failed\n");
			//snd_pcm_recover (playback_handle, frames_played, 0);
			break;
		}
	}

	snd_pcm_close(playback_handle);
	snd_pcm_close(capture_handle);
	exit(0);
}



