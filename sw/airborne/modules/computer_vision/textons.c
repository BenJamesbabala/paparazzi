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

// Own header
#include "modules/computer_vision/cv.h"
#include "modules/computer_vision/textons.h"

#include <stdio.h>

float ****dictionary;
uint32_t learned_samples;
uint8_t dictionary_initialized;

// File pointer for saving the dictionary
static FILE *dictionary_logger = NULL;

/**
 * Main texton processing function that first either loads or learns a dictionary and then extracts the texton histogram.
 * @param[out] *img The output image
 * @param[in] *img The input image (YUV422)
 */
struct image_t* texton_func(struct image_t* img);
struct image_t* texton_func(struct image_t* img)
{
    // extract frame from img struct:
    uint8_t *frame = (uint8_t *)img->buf;

    // if dictionary not initialized:
	if(dictionary_ready == 0)
	{
        if(load_dictionary == 0)
        {
            // Train the dictionary:
    		DictionaryTrainingYUV(dictionary, frame, n_textons, patch_size, learned_samples, n_samples_image, alpha,
				input->w, input->h, dictionary_initialized);

            // After a number of samples, stop learning:
            if(learned_samples >= n_learning_samples)
		    {
                // Save the dictionary:
                save_dictionary();
                // stop learning:
			    dictionary_ready = 1;      
			    // lower learning rate
    			alpha = 0.0;
            }
        }
        else
        {
            // Load the dictionary:
            load_dictionary();
        }
    }
    else
    {
        // Extract distributions
		DistributionExtraction(dictionary, frame, texton_distribution, n_textons, patch_size, n_samples_image, RANDOM_SAMPLES,
				input->w, input->h, border_width, border_height);
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
		dictionary_intialized = 1;
	}
	else
	{
		// ********
		// LEARNING
		// ********

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
 * Save the texton dictionary.
 */
void save_dictionary()
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
void load_dictionary()
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
    dictionary_initialized = 0;
    learned_samples = 0;
    dictionary_ready = 0;
    cv_add(texton_func);
}

