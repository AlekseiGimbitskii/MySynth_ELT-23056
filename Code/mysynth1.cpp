#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <poll.h>
#include <alsa/asoundlib.h>

#define PCM_DEVICE "default"

snd_pcm_t *playback_handle;
short buf[4096];

struct confData{
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_sw_params_t *sw_params;	
	unsigned int sample_rate;
};

int playback_callback(snd_pcm_sframes_t nframes)
{
	int err;

	printf("playback callback called with %lu frames\n", nframes);

	/* ... fill buf with data ... */
	for (int i = 0; i < 4096; i++)
	{
		short x = std::rand() / 256;
		buf[i] = x;
	}

	err = snd_pcm_writei(playback_handle, buf, nframes);
	if (err < 0)
	{
		fprintf(stderr, "write failed (%s)\n", snd_strerror(err));
	}

	return err;
}

int open_and_init(struct confData *conf)
{
	int err;

	err = snd_pcm_open(&playback_handle, PCM_DEVICE, SND_PCM_STREAM_PLAYBACK, 0);
	if (err < 0)
	{
		fprintf(stderr, "cannot open audio device %s (%s)\n",
				PCM_DEVICE,
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_malloc(&conf->hw_params);
	if (err < 0)
	{
		fprintf(stderr, "cannot allocate hardware parameter structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_any(playback_handle, conf->hw_params);
	if (err < 0)
	{
		fprintf(stderr, "cannot initialize hardware parameter structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_set_access(playback_handle, conf->hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0)
	{
		fprintf(stderr, "cannot set access type (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_set_format(playback_handle, conf->hw_params, SND_PCM_FORMAT_S16_LE);
	if (err < 0)
	{
		fprintf(stderr, "cannot set sample format (%s)\n",
				snd_strerror(err));
		exit(1);
	}
	/*
	snd_pcm_hw_params_set_access(playback_handle, hw_params, SND_PCM_ACCESS_RW_NONINTERLEAVED);
	snd_pcm_hw_params_set_format(playback_handle, hw_params, SND_PCM_FORMAT_S16_LE);*/

	err = snd_pcm_hw_params_set_rate_near(playback_handle, conf->hw_params, &conf->sample_rate, 0);
	if (err < 0)
	{
		fprintf(stderr, "cannot set sample rate (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_set_channels(playback_handle, conf->hw_params, 1);
	if (err < 0)
	{
		fprintf(stderr, "cannot set channel count (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params(playback_handle, conf->hw_params);
	if (err < 0)
	{
		fprintf(stderr, "cannot set parameters (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	snd_pcm_hw_params_free(conf->hw_params);

	/* tell ALSA to wake us up whenever 4096 or more frames
		   of playback data can be delivered. Also, tell
		   ALSA that we'll start the device ourselves.
		*/

	err = snd_pcm_sw_params_malloc(&conf->sw_params);
	if (err < 0)
	{
		fprintf(stderr, "cannot allocate software parameters structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_sw_params_current(playback_handle, conf->sw_params);
	if (err < 0)
	{
		fprintf(stderr, "cannot initialize software parameters structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_sw_params_set_avail_min(playback_handle, conf->sw_params, 4096);
	if (err < 0)
	{
		fprintf(stderr, "cannot set minimum available count (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_sw_params_set_start_threshold(playback_handle, conf->sw_params, 0U);
	if (err < 0)
	{
		fprintf(stderr, "cannot set start mode (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_sw_params(playback_handle, conf->sw_params);
	if (err < 0)
	{
		fprintf(stderr, "cannot set software parameters (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	/* the interface will interrupt the kernel every 4096 frames, and ALSA
		   will wake up this program very soon after that.
		*/

	err = snd_pcm_prepare(playback_handle);
	if (err < 0)
	{
		fprintf(stderr, "cannot prepare audio interface for use (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	return 0;
}

main(int argc, char *argv[])
{
	int err;
	int frames_played;
	snd_pcm_sframes_t frames_to_deliver;
	struct confData *conf;
	conf->sample_rate = (unsigned int)44100; // set sample rate and device names

	open_and_init(conf);

	while (1)
	{

		/* wait till the interface is ready for data, or 1 second
			   has elapsed.
			*/

		if ((err = snd_pcm_wait(playback_handle, 1000)) < 0)
		{
			fprintf(stderr, "poll failed (%s)\n", strerror(errno));
			break;
		}

		/* find out how much space is available for playback data */

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

		frames_to_deliver = frames_to_deliver > 4096 ? 4096 : frames_to_deliver;

		/* deliver the data */
		frames_played= playback_callback(frames_to_deliver);
		if (frames_played != frames_to_deliver)
		{
			fprintf(stderr, "playback callback failed\n");
			break;
		}
	}
	snd_pcm_close(playback_handle);
	exit(0);
}