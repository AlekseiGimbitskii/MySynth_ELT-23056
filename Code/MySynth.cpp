#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <poll.h>
#include <alsa/asoundlib.h>

#include <stk/Noise.h>

#define PLAYBACK_DEVICE "default"
#define CAPTURE_DEVICE "hw:4,0"

#define PCM_DEVICE "default"
#define BUF_SIZE 4096

// g++ MySynth.cpp -o MySynth.o -lasound
snd_pcm_t *capture_handle;
snd_pcm_t *playback_handle;

struct confData {
	snd_pcm_hw_params_t *hw_playback_params;
	snd_pcm_sw_params_t *sw_playback_params;
	snd_pcm_hw_params_t *hw_capture_params;
	snd_pcm_sw_params_t *sw_capture_params;
	unsigned int sample_rate;
};

void dummyFilter(short buf[]) {
	buf[1] = buf[1] - buf[0];
	for(int i = 2; i < BUF_SIZE; i++ ){
		buf[i] = buf[i] - buf[i - 1] - buf[i - 2];
	}
}

int playback_callback(snd_pcm_sframes_t nframes, short buf[]) {
	int err;

	printf("playback callback called with %lu frames\n", nframes);

	err = snd_pcm_writei(playback_handle, buf, nframes);
	if (err < 0) {
		fprintf(stderr, "write failed (%s)\n", snd_strerror(err));
	}

	return err;
}

int capture_callback(snd_pcm_sframes_t nframes, short buf[]) {

	int err;

	printf("capture callback called with %lu frames\n", nframes);

	/* ... fill buf with data ... */
	err = snd_pcm_readi(capture_handle, buf, nframes);
	if (err != nframes)	{
		fprintf(stderr, "read failed (%s)\n", snd_strerror(err));
	}

	return err;
}

int open_and_init(struct confData *conf)
{
	int err;

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
	err = snd_pcm_hw_params_malloc(&conf->hw_playback_params);
	if (err < 0) {
		fprintf(stderr, "cannot allocate playback hardware parameter structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_malloc(&conf->hw_capture_params);
	if (err < 0) {
		fprintf(stderr, "cannot allocate capture hardware parameter structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	// init hw_param structures
	err = snd_pcm_hw_params_any(playback_handle, conf->hw_playback_params);
	if (err < 0) {
		fprintf(stderr, "cannot initialize playback hardware parameter structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_any(capture_handle, conf->hw_capture_params);
	if (err < 0) {
		fprintf(stderr, "cannot initialize capture hardware parameter structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	//set access type
	err = snd_pcm_hw_params_set_access(playback_handle, conf->hw_playback_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0) {
		fprintf(stderr, "cannot set access type (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_set_access(capture_handle, conf->hw_capture_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0) {
		fprintf(stderr, "cannot set access type (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	//set sample formats
	err = snd_pcm_hw_params_set_format(playback_handle, conf->hw_playback_params, SND_PCM_FORMAT_S16_LE);
	if (err < 0) {
		fprintf(stderr, "cannot set sample format (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_set_format(capture_handle, conf->hw_capture_params, SND_PCM_FORMAT_S16_LE);
	if (err < 0) {
		fprintf(stderr, "cannot set sample format (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	//set sample rate
	err = snd_pcm_hw_params_set_rate_near(playback_handle, conf->hw_playback_params, &conf->sample_rate, 0);
	if (err < 0) {
		fprintf(stderr, "cannot set sample rate (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_set_rate_near(capture_handle, conf->hw_capture_params, &conf->sample_rate, 0);
	if (err < 0) {
		fprintf(stderr, "cannot set sample rate (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	// set chanel count
	err = snd_pcm_hw_params_set_channels(playback_handle, conf->hw_playback_params, 1);
	if (err < 0) {
		fprintf(stderr, "cannot set channel count (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_set_channels(capture_handle, conf->hw_capture_params, 1);
	if (err < 0) {
		fprintf(stderr, "cannot set channel count (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	// set parameters
	err = snd_pcm_hw_params(playback_handle, conf->hw_playback_params);
	if (err < 0) {
		fprintf(stderr, "cannot set parameters (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params(capture_handle, conf->hw_capture_params);
	if (err < 0) {
		fprintf(stderr, "cannot set parameters (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	//free params
	snd_pcm_hw_params_free(conf->hw_playback_params);
	snd_pcm_hw_params_free(conf->hw_capture_params);

	/* tell ALSA to wake us up whenever 4096 or more frames
		   of playback data can be delivered. Also, tell
		   ALSA that we'll start the device ourselves.
		*/

	err = snd_pcm_sw_params_malloc(&conf->sw_playback_params);
	if (err < 0) {
		fprintf(stderr, "cannot allocate software parameters structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_sw_params_current(playback_handle, conf->sw_playback_params);
	if (err < 0) {
		fprintf(stderr, "cannot initialize software parameters structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_sw_params_set_avail_min(playback_handle, conf->sw_playback_params, 4096);
	if (err < 0) {
		fprintf(stderr, "cannot set minimum available count (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_sw_params_set_start_threshold(playback_handle, conf->sw_playback_params, 0U);
	if (err < 0) {
		fprintf(stderr, "cannot set start mode (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_sw_params(playback_handle, conf->sw_playback_params);
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

	struct confData conf;
	conf.sample_rate = (unsigned int)44100; // set sample rate
	int err;
	int frames_played;
	int frames_captured;
	snd_pcm_sframes_t frames_to_deliver = BUF_SIZE;

	open_and_init(&conf);

	short buf[BUF_SIZE];
	while (1) {

		/* wait till the playback device is ready for data, or 1 second
		 * has elapsed.
		 */

		if ((err = snd_pcm_wait(playback_handle, 1000)) < 0) {
			fprintf(stderr, "poll failed (%s)\n", strerror(errno));
			break;
		}

		/* wait till the capture device is ready for data, or 1 second
		 * has elapsed.
		 */
		if ((err = snd_pcm_wait(capture_handle, 1000)) < 0) {
			fprintf(stderr, "poll failed (%s)\n", strerror(errno));
			break;
		}

		/* find out how much space is available for playback data */
		/*
		frames_to_deliver = snd_pcm_avail_update(playback_handle);
		if (frames_to_deliver < 0)
		{
			if (frames_to_deliver == -EPIPE)
			{
				fprintf(stderr, "an xrun occured\n");
				break;
			}
			else
			{
				fprintf(stderr, "unknown ALSA avail update return value (%ld)\n",
						frames_to_deliver);
				break;
			}
		}

		frames_to_deliver = frames_to_deliver > BUF_SIZE ? BUF_SIZE : frames_to_deliver;
		*/


		// capture data
		frames_captured = capture_callback(frames_to_deliver, buf);
		if (frames_captured != frames_to_deliver) {
			fprintf(stderr, "capture callback failed\n");
			break;
		}


		//apply dummy filter
		dummyFilter(buf);

		// prpare playback device
		err = snd_pcm_prepare (playback_handle);
		if (err < 0) {
			fprintf (stderr, "cannot prepare playback interface for use (%s)\n",
				 snd_strerror (err));
			exit (1);
		}
		/* deliver the data */
		frames_played = playback_callback(frames_to_deliver, buf);
		if (frames_played != frames_to_deliver) {
			fprintf(stderr, "playback callback failed\n");
			break;
		}
	}
	snd_pcm_close(playback_handle);
	exit(0);
}

using namespace stk;
int our_noise(void)
{
	StkFloat output;
	Noise noise;
	for ( unsigned int i=0; i<20; i++ ) {
		output = noise.tick();
		std::cout << "i = " << i << " : output = " << output << std::endl;
	}
	return 0;
}
