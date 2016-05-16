/*
 * Copyright (C) 2016, Hann Woei Ho, Guido de Croon
 *
 * This file is part of Paparazzi.
 *
 * Paparazzi is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * Paparazzi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Paparazzi; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * @file modules/computer_vision/textons.c
 *
 * Takes an image and represents the texture and colors in the image with a texton histogram.
 * A texton is a cluster centroid in a space populated by image patches. First, this code
 * learns or loads a texton dictionary. Then, for each incoming image, patches are sampled from
 * the image, compared to textons in the dictionary, and the closest texton is identified,
 * augmenting the corresponding bin in the texton histogram.
 */

#include <stdlib.h>
#include <stdio.h>
#include "modules/computer_vision/cv.h"
#include "modules/computer_vision/textons.h"

float ****dictionary;
uint32_t learned_samples = 0;
uint8_t dictionary_initialized = 0;

// initial settings:
float *texton_distribution;
uint8_t load_dictionary = 1;
uint8_t alpha_uint = 10;
uint8_t n_textons = 30;
uint8_t patch_size = 5; 
uint32_t n_learning_samples = 100000;
uint32_t n_samples_image = 100;
uint8_t FULL_SAMPLING = 0;
uint32_t border_width = 0;
uint32_t border_height = 0;
uint8_t dictionary_number = 0;

// status variables
uint8_t dictionary_ready = 0;
float alpha = 0.0;

// File pointer for saving the dictionary
static FILE *dictionary_logger = NULL;
#ifndef DICTIONARY_PATH
#define DICTIONARY_PATH /data/video/
#endif

/**
 * Main texton processing function that first either loads or learns a dictionary and then extracts the texton histogram.
 * @param[out] *img The output image
 * @param[in] *img The input image (YUV422)
 */
struct image_t* texton_func(struct image_t* img);
struct image_t* texton_func(struct image_t* img)
{

    printf("B\n");
    // extract frame from img struct:
    uint8_t *frame = (uint8_t *)img->buf;

  // if patch size odd, correct:
  if(patch_size % 2 == 1) patch_size++;

    // if dictionary not initialized:
	if(dictionary_ready == 0)
	{
        if(load_dictionary == 0)
        {
            // Train the dictionary:
    		DictionaryTrainingYUV(frame, img->w, img->h);

            // After a number of samples, stop learning:
            if(learned_samples >= n_learning_samples)
		    {
                // Save the dictionary:
                save_texton_dictionary();
                // stop learning:
			    dictionary_ready = 1;      
			    // lower learning rate
    			alpha = 0.0;
            }
        }
        else
        {
            // Load the dictionary:
            load_texton_dictionary();
        }
    }
    else
    {
        // Extract distributions
		DistributionExtraction(frame, img->w, img->h);
    }

  return img; // Colorfilter did not make a new image
}

/**
 * Function that performs one pass for dictionary training. It extracts samples from an image, finds the closest texton
 * and moves it towards the sample. 
 * @param[in] frame* The YUV image data
 * @param[in] width The width of the image
 * @param[in] height The height of the image
 */
void DictionaryTrainingYUV(uint8_t *frame, uint16_t width, uint16_t height)
{
	int i, j, w, s, texton, c; // iterators
	int x,y; // image coordinates
	float error_texton; // distance between an image patch and a texton

	uint8_t *buf;

	// ***********************
	//   DICTIONARY LEARNING
	// ***********************

	if(!dictionary_initialized)
	{
		// **************
		// INITIALISATION
		// **************

    printf("Intiailizing dictionary!\n");

		// in the first image, we initialize the textons to random patches in the image
		for(w = 0; w < n_textons; w++)
		{
			// select a coordinate
			x = rand() % (width - patch_size);
			y = rand() % (height - patch_size);

			// take the sample
			for(i = 0; i < patch_size; i++)
			{
				buf = frame + (width * 2 * (i+y)) + 2*x;
				for(j = 0; j < patch_size; j++)
				{
					// put it in a texton
					// U/V component
					dictionary[w][i][j][0] = (float) *buf;
					buf += 1;
					// Y1/Y2 component
					dictionary[w][i][j][1] = (float) *buf;
					buf += 1;
				}
			}
		}
		dictionary_initialized = 1;
	}
	else
	{
		// ********
		// LEARNING
		// ********
    printf("Learning!");
    alpha = ((float) alpha_uint) / 255.0;

		float *texton_distances, ***patch;
		texton_distances = (float *)calloc(n_textons,sizeof(float));
		patch = (float ***)calloc(patch_size,sizeof(float**));

		for(i = 0; i < patch_size; i++)
		{
			patch[i] = (float **)calloc(patch_size,sizeof(float*));
			for(j = 0; j < patch_size;j++)
			{
				patch[i][j] = (float *)calloc(2,sizeof(float));
			}
		}

        // Extract and learn from n_samples_image per image
		for(s = 0; s < n_samples_image; s++)
		{
			// select a random sample from the image
			x = rand() % (width - patch_size);
			y = rand() % (height - patch_size);

			// reset texton_distances
			for(texton = 0; texton < n_textons; texton++)
			{
				texton_distances[texton] = 0;
			}

			// extract sample
			for(i = 0; i < patch_size; i++)
			{
				buf = frame + (width * 2 * (i+y)) + 2*x;
				for(j = 0; j < patch_size; j++)
				{
                    // TODO: I think this is incorrect, and it should be YUYV:
					// U/V component
		        	patch[i][j][0] = (float) *buf;
					buf += 1;
					// Y1/Y2 component
					patch[i][j][1] = (float) *buf;
					buf += 1;
				}
			}

			// determine distances to the textons:
			for(i = 0; i < patch_size; i++)
			{
				for(j = 0; j < patch_size; j++)
				{
					for(c = 0; c < 2; c++)
					{
						// determine the distance to textons
						for(texton = 0; texton < n_textons; texton++)
						{
							texton_distances[texton] += (patch[i][j][c] - dictionary[texton][i][j][c])
													* (patch[i][j][c] - dictionary[texton][i][j][c]);
						}
					}
				}
			}

			// search the closest texton
			int assignment = 0;
			float min_dist = texton_distances[0];
			for(texton = 1; texton < n_textons; texton++)
			{
				if(texton_distances[texton] < min_dist)
				{
					min_dist = texton_distances[texton];
					assignment = texton;
				}
			}

			// move the neighbour closer to the input
			for(i = 0; i < patch_size; i++)
			{
				for(j = 0; j < patch_size; j++)
				{
					for(c = 0; c < 2; c++)
					{
						error_texton = patch[i][j][c] - dictionary[assignment][i][j][c];
						dictionary[assignment][i][j][c] += (alpha * error_texton);
					}
				}
			}
            
            // Augment the number of learned samples:
			learned_samples++;
		}

        // Free the allocated memory:
		for(i = 0; i < patch_size; i++)
		{
			for(j = 0; j < patch_size; j++)
			{
                free(patch[i][j]);
			}
			free(patch[i]);
		}
		free(patch);
		free(texton_distances);
	}
    
    // Free the buffer
	buf = NULL;
	free(buf);
}

/**
 * Function that extracts a texton histogram from an image.
 * @param[in] frame* The YUV image data
 * @param[in] width The width of the image
 * @param[in] height The height of the image
 */
void DistributionExtraction(uint8_t *frame, uint16_t width, uint16_t height)
{
	int i, j, texton, c; // iterators
	int x, y; // coordinates
	int n_extracted_textons = 0;

	uint8_t *buf;

	// ************************
	//       EXECUTION
	// ************************

    // Allocate memory for texton distances and image patch:
	float *texton_distances, ***patch;
	texton_distances = (float *)calloc(n_textons,sizeof(float));
	patch = (float ***)calloc(patch_size,sizeof(float**));
	for(i = 0; i < patch_size; i++)
	{
		patch[i] = (float **)calloc(patch_size,sizeof(float*));
		for(j = 0; j < patch_size;j++)
		{
			patch[i][j] = (float *)calloc(2,sizeof(float));
		}
	}

	int finished = 0;
	x = 0;
	y = 0;
	while(!finished)
	{
		if(!FULL_SAMPLING)
		{
			x = border_width + rand() % (width - patch_size - 2*border_width);
			y = border_height + rand() % (height - patch_size - 2*border_height);
		}

		// reset texton_distances
		for(texton = 0; texton < n_textons; texton++)
		{
			texton_distances[texton] = 0;
		}

		// extract sample
		for(i = 0; i < patch_size; i++)
		{
			buf = frame + (width * 2 * (i+y)) + 2*x;
			for(j = 0; j < patch_size; j++)
			{
				// U/V component
	        	patch[i][j][0] = (float) *buf;
				buf += 1;
				// Y1/Y2 component
				patch[i][j][1] = (float) *buf;
				buf += 1;
			}
		}

		// determine distances:
		for(i = 0; i < patch_size; i++)
		{
			for(j = 0; j < patch_size; j++)
			{
				for(c = 0; c < 2; c++)
				{
					// determine the distance to words
					for(texton = 0; texton < n_textons; texton++)
					{
						texton_distances[texton] += (patch[i][j][c] - dictionary[texton][i][j][c])
												* (patch[i][j][c] - dictionary[texton][i][j][c]);
					}
				}
			}
		}

		// determine the nearest neighbour
		// search the closest centroid
		int assignment = 0;
		float min_dist = texton_distances[0];
		for(texton = 1; texton < n_textons; texton++)
		{
			if(texton_distances[texton] < min_dist)
			{
				min_dist = texton_distances[texton];
				assignment = texton;
			}
		}

		// put the assignment in the histogram
		texton_distribution[assignment]++;
		n_extracted_textons++;

		if(!FULL_SAMPLING && n_extracted_textons == n_samples_image)
		{
			finished = 1;
		}
		else
		{
            // FULL_SAMPLING is actually a sampling that covers the image:
			y += patch_size;
            // True full sampling would require:
			// y++;

			if(y > height - patch_size)
			{
				if(!FULL_SAMPLING)
					x += patch_size;
				else
					x++;
				y = 0;
			}
			if(x > width - patch_size)
			{
				finished = 1;
			}
		}
	}

	// Normalize distribution:
	for(i = 0; i < n_textons; i++)
	{
		texton_distribution[i] = texton_distribution[i] / (float) n_extracted_textons;
	}


    // free memory:
	for(i = 0; i < patch_size; i++)
	{
		for(j = 0; j < patch_size; j++)
		{
			   free(patch[i][j]);
		}
		free(patch[i]);
	}
	free(patch);
    free(texton_distances);

	buf = NULL;
	free(buf);

} // EXECUTION



/**
 * Save the texton dictionary.
 */
void save_texton_dictionary(void)
{
	//save a dictionary
	char filename[512];

	// Check for available files
	sprintf(filename, "%s/Dictionary_%05d.dat", STRINGIFY(DICTIONARY_PATH), dictionary_number);
	
    dictionary_logger = fopen(filename, "w");

	if(dictionary_logger == NULL)
	{
		perror("Error while opening the file.\n");
	}
	else
	{
		// (over-)write dictionary
		for(uint8_t i = 0; i < n_textons; i++)
		{
			for(uint8_t j = 0; j < patch_size;j++)
			{
				for(uint8_t k = 0; k < patch_size; k++)
				{
					fprintf(dictionary_logger, "%f\n",dictionary[i][j][k][0]);
					fprintf(dictionary_logger, "%f\n",dictionary[i][j][k][1]);
				}
			}
		}
		fclose(dictionary_logger);
	}

}

/**
 * Load a texton dictionary.
 */
void load_texton_dictionary(void)
{
    char filename[512];
	sprintf(filename, "%s/Dictionary_%05d.dat", STRINGIFY(DICTIONARY_PATH), dictionary_number);
    
	if((dictionary_logger = fopen(filename, "r")))
	{
        // Load the dictionary:
		for(int i = 0; i < n_textons; i++)
		{
			for(int j = 0; j < patch_size;j++)
			{
				for(int k = 0; k < patch_size; k++)
				{
					if(fscanf(dictionary_logger, "%f\n", &dictionary[i][j][k][0]) == EOF) break;
					if(fscanf(dictionary_logger, "%f\n", &dictionary[i][j][k][1]) == EOF) break;
				}
			}
		}

		fclose(dictionary_logger);
		dictionary_ready = 1;
	}
	else
	{
        // If the given dictionary does not exist, we start learning one:
        printf("Texton dictionary %d does not exist, we start learning one.\n", dictionary_number);
        load_dictionary = 0;
        learned_samples = 0;
        dictionary_initialized = 0;
	}
}

/**
 * Initialize
 */
void textons_init(void)
{
    printf("Textons init\n");
    texton_distribution = (float *)calloc(n_textons,sizeof(float));
    dictionary_initialized = 0;
    learned_samples = 0;
    dictionary_ready = 0;
    cv_add(texton_func);
}

void textons_stop(void) {
  free(texton_distribution);
}
