/**********************************************************************************************************************

japacker.h - Just Another image Packer - A fast, efficient and easy to use packing library for texture atlas generation

This code is heavily commented and is intended to be easy to follow.

The problem: you want to pack as many images into a larger image, either to avoid image fragmentation (and many disk
accesses) or to help with rendering performance. What's the best way to go about it?

In japacker, the following approach is taken:

The destination image is composed of two different types of areas: areas that already have packed images in it, and
areas that are still empty.

In simple terms, the algorithm cycles through all empty areas (which are sorted from smallest to largest) until it
finds the smallest one in which the current image to pack (which are sorted from largest to smallest) will fit.

At first, the whole destination image is an empty area. After the first image is packed, starting at the top left,
the destination image then has one packed image and two empty areas, like so:

     ____________________
    |       |            |
    |   A   |            |        A - Packed rectangle
    |       |            |
    |_______|            |
    |       |      C     |        B - Smaller empty area
    |       |            |
    |   B   |            |
    |       |            |        C - Larger empty area
    |_______|____________|
     

If the next rectangle is small enough, it will be added to the top left side of the B empty area (which will then be
further divided in two), otherwise it will be added to the top left side of the C empty area.

The rects to be packed are represented by an array of japacker_rect's, which is as large as num_rects. The user will
fill each japacker_rect in the array with the width and height of each image to pack, and after running the algorithm
each japacker_rect will have the image's x and y position and whether it should be rotated (if the option is set).

The user is then free to use that x and y information to draw the actual image to the destination as they please.

The empty areas are represented by a double linked list which is based on an array of japacker_empty_area's.
When a new empty area is created, it is sorted in the array, from smallest to largest. This linked list sorting is what
makes the code efficient, because it allows for fast insertion and removal of elements in the middle of the list,
regardless of the fact that, in memory, the empty areas are actually stored sequentially.

Also, to save memory, empty areas are "repurposed" when an image is packed. As seen above, before the "A" rect was
packed, there was only a single empty area, which A then occupied a part of. The original empty area's spot in memory
was where "C" now resides, but the original empty area's x position and width was reduced to allow for the "A" image
and "B" empty area to have their own space. Conversely, the "B" empty area was created new (it's a new item in the
array), but it will also be repurposed as one of its smaller empty area when an image is added there.

Since an image can create, at most, one new empty area (since one of the two new empty areas is repurposed), then the
empty area's array only needs, at most, to be as large as the total number of images plus one.

This knowledge allows setting the full memory block for the empty areas in advance, preventing constant array
increases and memory reallocations, thus improving performance.

The code is thread safe: you can use two different japacker_t structs in two different threads. However, you should NOT
use the same japacker_t struct in two different threads, as that will cause problems.


***********************************************************************************************************************

Example usage:

// 1. Create a japacker_t struct
japacker_t packer;

// 2. Run your code to calculate the total number of rectangles to pack
unsigned int num_rectangles_to_pack = calculate_total_images_to_pack();

// 3. Initialize the struct, checking for errors
if (japacker_init(&packer, num_rectangles_to_pack, ATLAS_WIDTH, ATLAS_HEIGHT) != JAPACKER_OK) {
    printf("There was an error initting the packer.\n");
    return;
}

// 4. Set the desired options
packer.options.allow_rotation = 1;
packer.options.sort_by = JAPACKER_SORT_BY_AREA;

// 5. Get the source images' width and height
image_wh *image_dimensions_list = get_all_image_dimensions();

// 6. Fill the rect inputs with the width and height
for (int i = 0; i < num_rects; i++) {
    packer.rects[i].input.width = image_dimensions_list[i].width;
    packer_rects[i].input.height = image_dimensions_list[i].height;
}

// 7. Pack the images
int result = japacker_pack(&packer);

// 8. japacker_pack() returns a number below JAPACKER_OK on error
if (result < JAPACKER_OK) {
    printf("There was an error packing the images.\n");
    return;
}

// 9. japacker_pack() returns the number of actually packed images on success
if (result != num_rectangles_to_pack) {
    printf("Not all rects were packed, only %d of %d.\n", result, num_rectangles_to_pack);
} else {
    printf("All %d rects packed.\n", result);
}

// 10. Create the destination image
pixel_array *dst_image = create_destination_image(ATLAS_WIDTH, ATLAS_HEIGHT);

// 11. Draw the images
for (int i = 0; i < num_rectangles_to_pack; i++) {

    // The rects struct hasn't been reordered so the index of the images is still the same as before
    japacker_rect *rect = packer.rects[i];

    // 12. Only work with rectangles that were packed
    if (!rect->output.packed) {
        continue;
    }

    // 13. Draw the image, checking for rotation. When rotated, the rect->input.width and rect->input.height
    //     remain unchanged. It's up to you to actually rotate the image to the destination buffer
    draw_to_image(dst_image, get_single_image(i), rect->output.x, rect->output.y, rect->output.rotated);
}

// 14. Once the packer is used, free it
japacker_free(&packer);


***********************************************************************************************************************

This library was developed by José Cadete (crudelios) and released as public domain and free for any use,
according to the terms below:

**************************************************************************

  THE UNLICENSE

  This is free and unencumbered software released into the public domain.

  Anyone is free to copy, modify, publish, use, compile, sell, or
  distribute this software, either in source code form or as a compiled
  binary, for any purpose, commercial or non-commercial, and by any
  means.

  In jurisdictions that recognize copyright laws, the author or authors
  of this software dedicate any and all copyright interest in the
  software to the public domain. We make this dedication for the benefit
  of the public at large and to the detriment of our heirs and
  successors. We intend this dedication to be an overt act of
  relinquishment in perpetuity of all present and future rights to this
  software under copyright law.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
  OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
  OTHER DEALINGS IN THE SOFTWARE.

  For more information, please refer to <http://unlicense.org/>

**********************************************************************************************************************/

#ifndef JAPACKER_H
#define JAPACKER_H

#include <math.h>
#include <stdlib.h>
#include <string.h>

/**
 * If you want to use this library in multiple places in your code, define JAPACKER_EXPORT before including this header
 * in the file where you want the functions to be defined, then define JAPACKER_IMPORT in the files where you want to
 * use the code.
 * 
 * If you don't set either define, all the functions will be declared static, which is fine if you want to use it only
 * on one file, but will lead to code duplication if you include japacker.h in multiple files of the same project.
 */
#ifdef JAPACKER_EXPORT
#define JAPACKER_DECL
#elif defined (JAPACKER_IMPORT)
#define JAPACKER_DECL extern
#else
#define JAPACKER_DECL static
#endif

/* Do not name mangle */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sets what happens when a rectangle is too large to be packed in the current image
 * 
 * The options are:
 * JAPACKER_STOP      - Immediately stops adding new rectangles and returns. This is the default action
 * JAPACKER_CONTINUE  - Ignores this rectangle and keeps packing the remaining ones that fit
 * JAPACKER_NEW_IMAGE - Keeps packing the rectangles that fit, keeps adding the ones that don't fit to new images
 *                      New images will be created untill all rectangles fit, unless the rectangle is too large to
 *                      even fit on an empty image
 */
typedef enum {
    JAPACKER_STOP     = 0,
    JAPACKER_CONTINUE = 1,
    JAPACKER_NEW_IMAGE = 2
} japacker_fail_policy;

/**
 * @brief Sets how rectangles and empty areas are internally sorted
 * 
 * The options are sorting the rectangles and empty areas by perimeter (default), area, height or width
 * Rectangles are always sorted by descending order (i.e. the largest ones first), while empty areas
 * are sorted in ascending order.
 * The packing efficiency may vary according to the sorting method used, so you may wish to try different
 * sorting methods.
 * As a rule of thumb, sorting by perimeter or by area is generally more efficient than by width or height.
 */
typedef enum {
    JAPACKER_SORT_BY_PERIMETER = 0,
    JAPACKER_SORT_BY_AREA      = 1,
    JAPACKER_SORT_BY_HEIGHT    = 2,
    JAPACKER_SORT_BY_WIDTH     = 3
} japacker_sort_type;

/**
 * @brief The errors that the public functions may return
 * 
 * The possible errors are:
 * JAPACKER_OK                     - No errors
 * JAPACKER_ERROR_WRONG_PARAMETERS - This usually means japacker_init() was not called before japacker_pack()
 * JAPACKER_ERROR_NO_MEMORY        - This means the computer ran out of memory while executing the code
 */
typedef enum {
    JAPACKER_OK                     =  0,
    JAPACKER_ERROR_WRONG_PARAMETERS = -1,
    JAPACKER_ERROR_NO_MEMORY        = -2
} japacker_error_type;

/**
 * @brief The base rectangle structure
 * 
 * japacker works by feeding an array of japacker_rect's, each one representing
 * an image that needs to be packed into a larger one
 * Only width and height should be provided by the user and will not be changed by the algorithm.
 * The rest of the variables will be filled by the algorithm and should be considered read-only.
 */
typedef struct japacker_rect {

    /** Input variables that the user needs to provide beforehand. */
    struct input {

        unsigned int width;  /**< The width of the rectangle to be packed. */
        
        unsigned int height; /**< The height of the rectangle to be packed. */

    } input;

    /** Output variables that are the result of running the algorithm. */
    struct output {

        unsigned int x;      /**< The x position of the packed rectangle in the target image. */
        
        unsigned int y;      /**< The y position of the packed rectangle in the target image. */

        int packed;          /**< Whether this rectangle was packed by the algorithm. */

        int rotated;         /**< Whether the rectangle had to be rotated to fit. Requires allow_rotation to be set.
                                  If so, width and height are not changed in the rect, but in the destination image
                                  the width should be used as the height, and vice-versa. */

        int image_index;     /**< The index of the image the rectangle was packed to.
                                  Requires setting fail_policy to JAPACKER_NEW_IMAGE. */

    } output;

} japacker_rect;

struct japacker_internal_data; // Documentation below

/**
 * @brief The main japacker structure, where the behaviour of the packer can be set.
 * 
 * Please refer to each variable's documentation for further usage instructions.
 */
typedef struct japacker_t {

    japacker_rect *rects; /**< A pointer to the list of rectanlges, generated after japacker_init()
                               is called. Its size is num_rects */

    /**
     * @brief Options that can be set before packing. If an option is not set, its default value is used.
     * 
     * Please refer to each variable's documentation for their meaning.
     */
    struct options {

        int allow_rotation;                /**< Whether to allow rectangles to be rotated if they don't fit normally.
                                                Defaults to 0. */

        int rects_are_sorted;              /**< Whether rects are already sorted.
                                                Defaults to 0.
                                                If you sorted the list of rectangles yourself,
                                                you can set this to 1 to prevent a needless resort. */

        int always_repack;                 /**< Whether to repack images that were already marked as packed when
                                                calling japacker_pack() consecutively.
                                                Defaults to 0. */

        int reduce_image_size;             /**< Whether to attempt reducing the last (or only) image's size to the
                                                smallest one that fits all the rectangles.
                                                Defaults to 0.
                                                Since this internally repacks the rectangles multiple times, this can
                                                be slow, so only use if you need the best possible packing
                                                efficiency or if processing time is not an issue. */

        japacker_sort_type sort_by;        /**< How to sort the rects and empty areas.
                                                Defaults to JAPACKER_SORT_BY_PERIMETER.
                                                Please refer to japacker_sort_type for details. */

        japacker_fail_policy fail_policy;  /**< What to do when an image doesn't fit.
                                                Defaults to JAPACKER_STOP.
                                                Please refer to japacker_fail_policy for details. */
    } options;

    /**
     * @brief Optional results, that may or may not be needed depending on the options set before packing.
     * 
     * Please refer to each variable's documentation for their meaning.
     */
    struct result {

        unsigned int images_needed;               /**< The number of destination images needed to pack all rects.
                                                       This variable is only useful is options.fail_policy is
                                                       set to JAPCKER_NEW_IMAGE, otherwise the value will always
                                                       be set to 1. */
        
        unsigned int last_image_width;            /**< The width of the latest destination rectangle used.
                                                       This variable is only needed if options.reduce_image_size
                                                       is set to 1, otherwise it will be the same value as the
                                                       provided width and height in japacker_init().
                                                       If result.images_needed is higher than 1, only the last image
                                                       uses result.last_image_width, the previous ones still use the
                                                       width and hegith provided in japacker_init(). */

        unsigned int last_image_height;           /**< Same as result.last_image_width, but for the height instead. */

    } result;

    struct japacker_internal_data *internal_data; /**< The struct that hold non-public japacker data.
                                                       See below for details */
} japacker_t;


/*
 * Forward declarations of public functions
 */

/**
 * @brief Initiates a japacker_t object, allocating memory as needed.
 * 
 * Don't use any other japacker_* function on a japacker_t without calling japacker_init() first.
 * 
 * @param packer The packer to init.
 * @param num_rectangles The total number of rectangles that need to be packed.
 * @param width The width of the destination rectangle.
 * @param height The height of the destination rectangle.
 * @return JAPACKER_OK on success, or another japacker_error_type result on error.
*/
JAPACKER_DECL int japacker_init(japacker_t *packer, unsigned int num_rectangles,
    unsigned int width, unsigned int height);

/**
 * @brief Resizes the destination image. Note that this won't automatically repack any rect already packed.
 * 
 * If you wish to repack the rects, make sure you set options.always_repack to 1 then call japacker_pack() again.
 * 
 * @param packer The packer whose destination image should be changed.
 * @param image_width The new width of the destination image.
 * @param image_height The new height of the destination image.
*/
JAPACKER_DECL void japacker_resize_image(japacker_t *packer, unsigned int image_width, unsigned int image_height);

/**
 * @brief Packs the rectangles into the destination image.
 * 
 * The actual packing performed will depend on whathever options you've set beforehand.
 * 
 * You can call japacker_pack() multiple times.
 * If options.always_repack is set to 1, it will repack every rect again,regardless of whether it was packed or not.
 * This is useful if you want to try repacking everything with different packing options, for example.
 * If options.always_repack is kept at 0, only rects that weren't packed yet will be packed.
 * This is useful if you want to manually pack to many destination images.
 * 
 * @param packer The japacker_t struct to pack.
 * @return One of japacker_error_type values on error, or the number of packed rects on success.
*/
JAPACKER_DECL int japacker_pack(japacker_t *packer);

/**
 * @brief Gets the offset of the x/y coordinates of a pixel of the destination image,
 * based on the x/y coordinates of a pixel of the source rect.
 * 
 * This is a helper function that does the x/y translation based on the rect's position on the image.
 * It also properly translates rotation.
 * If rotation is enabled, the destination pixel is retrieved assuming a counter-clockwise rotation.
 * If multiple images were needed, the offset will be calculated for the proper image.
 * While this function is helpful, it is also slower than direct pixel manipulation. Therefore only use this convenience
 * function if performance is not an issue.
 * 
 * @param packer The packer in use.
 * @param rect The rectangle to calculate the offset from.
 * @param x The x offset of the rectangle.
 * @param y The y offset of the rectangle.
 * @return An offset, assuming the destination image is in the format image[height][width] (or image[height * width]).
*/
JAPACKER_DECL unsigned int japacker_get_dst_offset(const japacker_t *packer, const japacker_rect *rect,
    unsigned int x, unsigned int y);

/**
 * @brief Frees the memory associated with a japacker_t object.
 * @param packer The object to free.
*/
JAPACKER_DECL void japacker_free(japacker_t *packer);



/***********************************************************************************************************************
 * Internal area below
 **********************************************************************************************************************/

#ifndef JAPACKER_IMPORT

/**
 * Percentage difference between the area of all the rectangles and the area of the destination image in which the
 * packer stops trying to shrink the destination image when options.reduce_image_size is set to 1.
 */
#define JAPACKER_TOLERABLE_AREA_DIFFERENCE_PERCENTAGE 2

/**
 * @brief A structure that defines an empty area inside the destination rectangle.
 * 
 * Please refer to each variable's documentation for their meaning.
 */
typedef struct japacker_empty_area {

    unsigned int x, y;                       /**< x and y position of the empty area. */

    unsigned int width, height;              /**< Width and height of the empty area. */

    unsigned int comparator;                 /**< The value upon which empty areas are sorted.
                                                  Please refer to the japacker_sort_type struct
                                                  or options.sort_by for details.
                                                  Precalculating the comparison factor upon creation
                                                  of the empty areas improves performance when sorting
                                                  through them, which is done a lot. */

    struct japacker_empty_area *prev, *next; /**< Pointer to the previous and next empty areas
                                                  in the sorted list. */
} japacker_empty_area;

/**
 * @brief A structure that holds the internal data of the packer.
 *
 * Please refer to each variable's documentation for their meaning.
 */
typedef struct japacker_internal_data {

    japacker_rect **sorted_rects; /**< An array of pointers to the rects provided by the user. This array can be sorted
                                       internally without changing the sort order provided by the user, allowing him to
                                       keep his own references to the rectangle array. */

    unsigned int num_rects;       /**< The total number of rectangles to pack */

    unsigned int image_width;     /**< The width of the destination image */

    unsigned int image_height;    /**< The height of the destination image */

    /**
     * @brief Structure that holds all the empty areas of the destination image.
     *
     * Please refer to each variable's documentation for their meaning.
     */
    struct empty_areas {

        struct japacker_empty_area *first;                 /**< Pointer to the first (smallest) empty area. */

        struct japacker_empty_area *last;                  /**< Pointer to the last (largest) empty area. */

        struct japacker_empty_area *list;                  /**< The actual unordered array where the empty areas are
                                                                stored. */

        int index;                                         /**< The highest index of the empty area array in use. */

        int size;                                          /**< The number of elements the array can hold. */

        void (*set_comparator)(japacker_empty_area *area); /**< A pointer to the function that sets the values which
                                                                are used to compare the empty areas to sort them.

                                                                The function can be one of the following:
                                                                - japacker_empty_area_set_perimeter_comparator()
                                                                - japacker_empty_area_set_area_comparator()
                                                                - japacker_empty_area_set_width_comparator()
                                                                - japacker_empty_area_set_height_comparator() */

    } empty_areas;

} japacker_internal_data;


/*
 * Sorting related functions
 */

/**
 * @brief Internal function to compare two rects by perimeter.
 * 
 * @param a The first rect.
 * @param b The second rect.
 * @return A positive number if the second rect is larger than the first, 0 if equal, a negative number otherwise.
*/
JAPACKER_DECL int japacker_compare_rect_perimeters(const void *a, const void *b)
{
    // Since we're just comparing perimeters and don't actually need the values,
    // we don't need to actually calculate them. Check documentation for qsort to see how this works
    return (*(japacker_rect **) b)->input.height + (*(japacker_rect **) b)->input.width -
           (*(japacker_rect **) a)->input.height - (*(japacker_rect **) a)->input.width;
}

/**
 * @brief Internal function to compare two rects by area.
 *
 * @param a The first rect.
 * @param b The second rect.
 * @return A positive number if the second rect is larger than the first, 0 if equal, a negative number otherwise.
*/
JAPACKER_DECL int japacker_compare_rect_areas(const void *a, const void *b)
{
    return (*(japacker_rect **) b)->input.height * (*(japacker_rect **) b)->input.width -
           (*(japacker_rect **) a)->input.height * (*(japacker_rect **) a)->input.width;
}

/**
 * @brief Internal function to compare two rects by height.
 *
 * @param a The first rect.
 * @param b The second rect.
 * @return A positive number if the second rect is larger than the first, 0 if equal, a negative number otherwise.
 */
JAPACKER_DECL int japacker_compare_rect_heights(const void *a, const void *b)
{
    return (*(japacker_rect **) b)->input.height - (*(japacker_rect **) a)->input.height;
}

/**
 * @brief Internal function to compare two rects by width.
 *
 * @param a The first rect.
 * @param b The second rect.
 * @return A positive number if the second rect is larger than the first, 0 if equal, a negative number otherwise.
 */
JAPACKER_DECL int japacker_compare_rect_widths(const void *a, const void *b)
{
    return (*(japacker_rect **) b)->input.width - (*(japacker_rect **) a)->input.width;
}

/**
 * @brief Internal function to create the empty area's comparator based on a perimeter sort.
 *
 * @param area The area to calculate the comparator for.
 */
JAPACKER_DECL void japacker_empty_area_set_perimeter_comparator(japacker_empty_area *area)
{
    area->comparator = area->height + area->width;
}

/**
 * @brief Internal function to create the empty area's comparator based on an area sort.
 *
 * @param area The area to calculate the comparator for.
 */
JAPACKER_DECL void japacker_empty_area_set_area_comparator(japacker_empty_area *area)
{
    area->comparator = area->height * area->width;
}

/**
 * @brief Internal function to create the empty area's comparator based on a width sort.
 *
 * @param area The area to calculate the comparator for.
 */
JAPACKER_DECL void japacker_empty_area_set_width_comparator(japacker_empty_area *area)
{
    area->comparator = area->width;
}

/**
 * @brief Internal function to create the empty area's comparator based on a height sort.
 *
 * @param area The area to calculate the comparator for.
 */
JAPACKER_DECL void japacker_empty_area_set_height_comparator(japacker_empty_area *area)
{
    area->comparator = area->height;
}

/**
 * @brief Sorts the rectangles.
 * 
 * The sorting used is based on options.sort_by. If options.rects_are_sorted is set, no sorting will occur.
 * However, the internal japacker_rects** will still be created and populated, since that's what's used internally.
 * 
 * @param packer The packer whose rects should be sorted.
 * @return 0 on error, 1 on success.
 */
JAPACKER_DECL int japacker_sort_rects(japacker_t *packer)
{
    japacker_internal_data *data = packer->internal_data;
    
    // To prevent changing the array of rects that the user provided,
    // we work with our own array of pointers to the user's rects, which we can then sort freely
    // without the user losing his own image index order
    data->sorted_rects = (japacker_rect **) malloc(data->num_rects * sizeof(japacker_rect *));

    if (!data->sorted_rects) {
        return 0;
    }

    for (unsigned int i = 0; i < data->num_rects; i++) {
        data->sorted_rects[i] = &packer->rects[i];
    }

    // Sort the rectangles if they aren't already sorted
    if (packer->options.rects_are_sorted != 1) {
        // Sort according to the type the user selected
        // The sort is always performed in descending order
        // By default, rectangles are sorted by their perimeter
        int (*sort_by)(const void *, const void *);

        switch (packer->options.sort_by) {
            case JAPACKER_SORT_BY_AREA:
                sort_by = japacker_compare_rect_areas;
                data->empty_areas.set_comparator = japacker_empty_area_set_area_comparator;
                break;
            case JAPACKER_SORT_BY_HEIGHT:
                sort_by = japacker_compare_rect_heights;
                data->empty_areas.set_comparator = japacker_empty_area_set_height_comparator;
                break;
            case JAPACKER_SORT_BY_WIDTH:
                sort_by = japacker_compare_rect_widths;
                data->empty_areas.set_comparator = japacker_empty_area_set_width_comparator;
                break;
            case JAPACKER_SORT_BY_PERIMETER:
            default:
                sort_by = japacker_compare_rect_perimeters;
                data->empty_areas.set_comparator = japacker_empty_area_set_perimeter_comparator;
                break;
        }

        qsort(data->sorted_rects, data->num_rects, sizeof(japacker_rect *), sort_by);
        packer->options.rects_are_sorted = 1;
    }

    return 1;
}


/*
 * Empty areas related functions
 */

/**
 * @brief Resets the empty areas, moving back to a single empty area the size of the entire image.
 * 
 * @param data The internal packer data to work with.
 * @param width The width of the new original empty area.
 * @param height The height of the new original empty area.
 */
JAPACKER_DECL void japacker_reset_empty_areas(japacker_internal_data *data, unsigned int width, unsigned int height)
{
    memset(data->empty_areas.list, 0, sizeof(japacker_empty_area) * data->empty_areas.size);

    data->empty_areas.index = 0;

    // The first empty space is always the entire area of the image
    data->empty_areas.first = &data->empty_areas.list[0];
    data->empty_areas.last = &data->empty_areas.list[0];
    data->empty_areas.first->width = width;
    data->empty_areas.first->height = height;
}

/**
 * @brief Sorts a newly created empty area in the list of empty areas.
 * 
 * This internal sorting goes backwards, going from largest to smallest, starting with current and
 * stopping when there are no smaller empty areas. The rationale is, since empty areas are created from larger ones,
 * then a new empty area can't be larger than the area of its parent.
 * This assumption (which, except for merges, is always true), prevents needing to cycle through the entire list, which
 * improves performance.
 * 
 * @param data The internal packer data to work with.
 * @param area The empty area to sort.
 * @param current The empty area on which to start searching to sort.
 */
JAPACKER_DECL void japacker_sort_empty_area(japacker_internal_data *data, japacker_empty_area *area,
    japacker_empty_area *current)
{
    // If there are no empty areas, then this becomes the only one
    if (!data->empty_areas.first) {
        data->empty_areas.first = area;
        data->empty_areas.last = area;
        return;
    }

    // We sort empty areas by comparator, with smaller comparatos first.
    // By sorting the empty areas by ascending comparison and the rects by descending comparison,
    // we make sure the largest rectangles search the smallest empty spaces first,
    // only stopping when they find the smallest possible empty space they will fit.

    // Check for the first empty space that has a lower perimeter than the current one
    while (current) {
        // If we find a smaller empty space on the list, we place the new one right after it
        if (current->comparator < area->comparator) {
            // If the found smaller empty area was actually the last, then the new empty area becomes the last instead
            if (current == data->empty_areas.last) {
                data->empty_areas.last = area;
            // Otherwise we just update the list linking references
            } else {
                area->next = current->next;
                area->next->prev = area;
            }
            area->prev = current;
            current->next = area;
            return;
        }
        current = current->prev;
    }

    // If no other empty area is larger than this one, then this area becomes the first empty area
    data->empty_areas.first->prev = area;
    area->next = data->empty_areas.first;
    data->empty_areas.first = area;
}

/**
 * @brief Removes an empty area from the sorted list.
 * 
 * @param data The internal packer data to work with.
 * @param area The empty area to remove from the list.
 */
JAPACKER_DECL void japacker_delist_empty_area(japacker_internal_data *data, japacker_empty_area* area)
{
    // If the area being removed from the list was the first, then the next area becomes the first
    if (area == data->empty_areas.first) {
        data->empty_areas.first = area->next;
    }
    // If the area being removed from the list was the last, then the previous area becomes the last
    if (area == data->empty_areas.last) {
        data->empty_areas.last = area->prev;
    }
    // Update list linking references
    if (area->prev) {
        area->prev->next = area->next;
    }
    if (area->next) {
        area->next->prev = area->prev;
    }
    area->prev = 0;
    area->next = 0;
}

/**
 * @brief Merges an empty area with adjacent empty areas.
 * 
 * This optimizes the empty area by making sure that areas that are directly on top or at the side of each other get
 * merged to a single, bigger empty area. This also improves performance as there are less empty areas to sort through.
 * 
 * @param data The internal packer data to work with.
 * @param area The empty area to which others should merge.
 * @return 1 if the empty area merged with adjacent empty areas, 0 otherwise.
*/
JAPACKER_DECL int japacker_merge_adjacent_empty_areas(japacker_internal_data *data, japacker_empty_area *area)
{
    for (japacker_empty_area *current = data->empty_areas.first; current; current = current->next) {
        int same_height = current->y == area->y && current->height == area->height;
        // Adjacent area to the left
        if (same_height && current->x + current->width == area->x) {
            area->x = current->x;
            area->width += current->width;
            japacker_delist_empty_area(data, current);
            japacker_merge_adjacent_empty_areas(data, area);
            return 1;
        }
        // Adjacent area to the right
        if (same_height && area->x + area->width == current->x) {
            area->width += current->width;
            japacker_delist_empty_area(data, current);
            japacker_merge_adjacent_empty_areas(data, area);
            return 1;
        }
        int same_width = current->x == area->x && current->width == area->width;
        // Adjacent area to the top
        if (same_width && current->y + current->height == area->y) {
            area->y = current->y;
            area->height += current->height;
            japacker_delist_empty_area(data, current);
            japacker_merge_adjacent_empty_areas(data, area);
            return 1;
        }
        // Adjacent area to the bottom
        if (same_width && area->y + area->height == current->y) {
            area->height += current->height;
            japacker_delist_empty_area(data, current);
            japacker_merge_adjacent_empty_areas(data, area);
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Splits an empty area into two smaller empty areas.
 * 
 * An empty area is split into two when a rectangle is created inside it. The creation of the rectangle takes a part of
 * the original empty area, and two new empty areas are created, one below and one to the side of the new rectangle.
 * 
 * @param data The internal packer data to work with.
 * @param area The empty area to split into two.
 * @param width The width of the rectangle that caused the split.
 * @param height The height of the rectangle that caused the split.
*/
JAPACKER_DECL void japacker_split_empty_area(japacker_internal_data *data, japacker_empty_area *area,
    unsigned int width, unsigned int height)
{
    japacker_empty_area *new_area = &data->empty_areas.list[++data->empty_areas.index];

    // First we check what's the remaining dimensions both to the right and below the new rectangle
    int remaining_width = area->width - width;
    int remaining_height = area->height - height;

    // If the empty area to the side of the new rectangle is larger than the one below,
    // one empty area will have the space directly below of the new rectangle,
    // and the other one will have all the space to the side
    // Do note that to save memory space, we change the old empty area's data to become one of the new empty areas
    if (remaining_width > remaining_height) {
        new_area->x = area->x;
        new_area->y = area->y + height;
        new_area->width = width;
        new_area->height = area->height - height;
        area->x += width;
        area->width -= width;

    // If the empty area below new rectangle is larger than the one to the side,
    // one empty area will have the space directly to the side of the new rectangle,
    // and the other one will have all the space below
    } else {
        new_area->x = area->x + width;
        new_area->y = area->y;
        new_area->width = area->width - width;
        new_area->height = height;
        area->y += height;
        area->height -= height;
    }

    japacker_empty_area *original_prev = area->prev;
    japacker_delist_empty_area(data, area);

    // Merge the empty areas with adjacent empty ones
    int merged = japacker_merge_adjacent_empty_areas(data, area) + japacker_merge_adjacent_empty_areas(data, new_area);

    // We can only set the comparator after merging, because the empty areas may be larger
    data->empty_areas.set_comparator(area);
    data->empty_areas.set_comparator(new_area);

    // If we aren't merging the new empty areas, we must still sort them, but we can optimize sorting due to the
    // following assumptions (which are always true):
    // - The new empty areas are smaller than the original empty area (which now has a rectangle)
    // - The newly created empty area is smaller than the repurposed empty area (we make sure of this)
    // Therefore we sort the new empty areas taking into account that the larger created area cannot be sorted
    // any higher than before, and the smaller created area cannot be sorted any higher than the larger created area.
    if (!merged) {
        japacker_sort_empty_area(data, area, original_prev);
        japacker_sort_empty_area(data, new_area, area->prev);
    } else {

        // If we're merging both new empty areas, we can't really optimize their sorting save for the fact that we can
        // limit the search for the smallest of the merged empty areas to start at the largest of the empty areas
        if (new_area->comparator < area->comparator) {
            japacker_sort_empty_area(data, area, data->empty_areas.last);
            japacker_sort_empty_area(data, new_area, area->prev);
        } else {
            japacker_sort_empty_area(data, new_area, data->empty_areas.last);
            japacker_sort_empty_area(data, area, new_area->prev);
        }
    }
}


/*
 * Packing related functions
 */

/**
 * @brief Packs a single rect.
 * 
 * If allow_rotation is 1 and the packer can't fit the rectangle, it'll attempt to rotate the rectangle and place it.
 * 
 * @param data The internal packer data to work with.
 * @param rect The rectangle to pack.
 * @param allow_rotation Whether to allow the rect to be rotated if it doesn't originally fit.
 * @return 1 if the rectangle was packed, 0 otherwise.
*/

JAPACKER_DECL int japacker_pack_rect(japacker_internal_data *data, japacker_rect *rect, int allow_rotation)
{
    unsigned int width, height;
    if (!rect->output.rotated) {
        width = rect->input.width;
        height = rect->input.height;
    } else {
        width = rect->input.height;
        height = rect->input.width;
    }
    for (japacker_empty_area *area = data->empty_areas.first; area; area = area->next)
    {
        // If the rectangle is larger than the current empty area, we must look for a larger empty area
        if (height > area->height || width > area->width) {
            continue;
        }

        // If the rectangle fits in this empty area, we place it here
        rect->output.x = area->x;
        rect->output.y = area->y;
        rect->output.packed = 1;

        // If the rectangle has the same dimensions as the empty area, we simply remove the empty area
        if (height == area->height && width == area->width) {
            japacker_delist_empty_area(data, area);
            return 1;
        }

        // If the rectangle has the same height but lower width,
        // we reduce the empty area's width and offset it to start to the right of the new rectangle
        if (height == area->height) {
            area->x += width;
            area->width -= width;
            japacker_empty_area *prev = area->prev;
            japacker_delist_empty_area(data, area);
            if (japacker_merge_adjacent_empty_areas(data, area)) {
                prev = data->empty_areas.last;
            }
            data->empty_areas.set_comparator(area);
            japacker_sort_empty_area(data, area, prev);
            return 1;
        }

        // If the rectangle has the same width but lower height,
        // we reduce the empty area's height and offset it to start below the new rectangle
        if (width == area->width) {
            area->y += height;
            area->height -= height;
            japacker_empty_area *prev = area->prev;
            japacker_delist_empty_area(data, area);
            if (japacker_merge_adjacent_empty_areas(data, area)) {
                prev = data->empty_areas.last;
            }
            data->empty_areas.set_comparator(area);
            japacker_sort_empty_area(data, area, prev);
            return 1;
        }

        // If the new rectangle has both a lower width and height than the empty area,
        // we split the empty area into two new empty areas, one at the right and one below the new rectangle
        japacker_split_empty_area(data, area, width, height);
        return 1;
    }

    // If the rectangle didn't fit anywhere and rotation is allowed, we try rotating the rectangle
    if (allow_rotation) {
        rect->output.rotated = 1;
        return japacker_pack_rect(data, rect, 0);
    }
    // We might have failed to pack a rotated rectangle, so we move it back to the original position
    // just in case it'll be packed to a new image
    rect->output.rotated = 0;
    return 0;
}

/**
 * @brief Reduces the size of the last created image.
 * 
 * This is a convenience function designed to improve the efficiency of packing, by preventing an image from being too
 * large for the number of rectangles it has.
 * 
 * This code works by finding the minimum possible area of the destination image (which is the sum of the areas of all
 * its rects), the area of the destination image set by the user, then, keep dividing the difference by two and
 * attempting to pack in a loop. The difference is divided by two for each pass of the loop.
 * 
 * If packing is successful, the current difference is subtracted to the width and height of the destination rectangle.
 * If it's unsuccessful, the difference is added.
 * 
 * This keeps happening until either the difference is smaller than 1 or there's a successful packing with a difference
 * of less than JAPACKER_TOLERABLE_AREA_DIFFERENCE_PERCENTAGE.
 * 
 * @param packer The packer in use.
 * @param rects_area The minimum possible area the destination image can have.
*/
JAPACKER_DECL void japacker_reduce_last_image_size(japacker_t *packer, unsigned int rects_area)
{
    japacker_internal_data *data = packer->internal_data;

    // We are going to get the difference between the current area and the actual area of the
    // inserted rects and work from there
    unsigned int current_area = data->image_width * data->image_height;

    // Don't look further if the difference between the rects' area and the image area is low enough
    if (current_area * 100 / rects_area < 100 + JAPACKER_TOLERABLE_AREA_DIFFERENCE_PERCENTAGE) {
        return;
    }

    // The image index to be used is the one for the last image
    unsigned int image_index = packer->result.images_needed - 1;

    // Get the proportional width and height for the used area
    float image_ratio = data->image_width / (float) data->image_height;
    unsigned int needed_width = (unsigned int) sqrt(rects_area * image_ratio) + 1;
    unsigned int needed_height = (unsigned int) sqrt(rects_area / image_ratio) + 1;

    // To get the best rectangle, we find the difference between the requested image's width and height and the
    // rect area's proportional width and height. We start our work from the middle of that difference
    unsigned int delta_width = (data->image_width - needed_width) / 2;
    unsigned int delta_height = (data->image_height - needed_height) / 2;

    // Use the last successful width and height as a measure for the best packing
    unsigned int last_successful_width = data->image_width;
    unsigned int last_successful_height = data->image_height;

    while (delta_width && delta_height) {
        // If the last packing was a failure, we increase the image size, otherwise we decrease it
        if (last_successful_width == packer->result.last_image_width) {
            packer->result.last_image_width -= delta_width;
            packer->result.last_image_height -= delta_height;
        } else {
            packer->result.last_image_width += delta_width;
            packer->result.last_image_height += delta_height;
        }

        japacker_reset_empty_areas(data, packer->result.last_image_width, packer->result.last_image_height);

        int failed_to_pack = 0;

        for (unsigned int i = 0; i < data->num_rects; i++) {
            japacker_rect *rect = data->sorted_rects[i];

            // Only repack the rects for the last image
            if (rect->output.image_index != image_index) {
                continue;
            }

            // Pack the rectangle. If packing fails, we must increase the image size
            if (!japacker_pack_rect(data, rect, packer->options.allow_rotation)) {
                failed_to_pack = 1;
                break;
            }
        }

        // Set the latest successful size
        if (!failed_to_pack) {
            last_successful_width = packer->result.last_image_width;
            last_successful_height = packer->result.last_image_height;

            unsigned int area_percentage_difference = last_successful_width * last_successful_height * 100 / rects_area;

            // Don't look further if the difference between the rects' area and the image area is low enough
            if (area_percentage_difference < 100 + JAPACKER_TOLERABLE_AREA_DIFFERENCE_PERCENTAGE) {
                return;
            }
        }

        // Reduce the deltas
        delta_width /= 2;
        delta_height /= 2;
    }

    // Reset to the latest successful packing if the final attempts failed
    if (last_successful_width != packer->result.last_image_width) {
        packer->result.last_image_width = last_successful_width;
        packer->result.last_image_height = last_successful_height;

        japacker_reset_empty_areas(data, packer->result.last_image_width, packer->result.last_image_height);

        for (unsigned int i = 0; i < data->num_rects; i++) {
            // Only repack the rects for the last image
            if (data->sorted_rects[i]->output.image_index != image_index) {
                continue;
            }
            japacker_pack_rect(data, data->sorted_rects[i], packer->options.allow_rotation);
        }
    }
}



/***********************************************************************************************************************
 * Public functions' implementation
 **********************************************************************************************************************/

JAPACKER_DECL int japacker_init(japacker_t *packer, unsigned int num_rectangles,
    unsigned int width, unsigned int height)
{
    // Clear all memory
    memset(packer, 0, sizeof(japacker_t));

    // Create the internal data
    japacker_internal_data *data = (japacker_internal_data *) malloc(sizeof(japacker_internal_data));

    if (!data) {
        return JAPACKER_ERROR_NO_MEMORY;
    }

    memset(data, 0, sizeof(japacker_internal_data));
    packer->internal_data = data;

    // Create the structure with the rectangles
    packer->rects = (japacker_rect *) malloc(sizeof(japacker_rect) * num_rectangles);
    if (!packer->rects) {
        return JAPACKER_ERROR_NO_MEMORY;
    }
    memset(packer->rects, 0, sizeof(japacker_rect) * num_rectangles);
    data->num_rects = num_rectangles;
    data->image_width = width;
    data->image_height = height;

    // Create the structure with the empty areas
    // Since a rect creates, at most, one new empty area, then the maximum possible number of empty areas
    // is equal to the number of rects, plus one for the original image
    unsigned int size = data->num_rects + 1;
    data->empty_areas.list = (japacker_empty_area *) malloc(size * sizeof(japacker_empty_area));
    if (!data->empty_areas.list) {
        return JAPACKER_ERROR_NO_MEMORY;
    }
    data->empty_areas.size = size;

    return JAPACKER_OK;
}

JAPACKER_DECL void japacker_resize_image(japacker_t *packer, unsigned int image_width, unsigned int image_height)
{
    packer->internal_data->image_width = image_width;
    packer->internal_data->image_height = image_height;
}

JAPACKER_DECL int japacker_pack(japacker_t *packer)
{
    japacker_internal_data *data = packer->internal_data;

    // Make sure the struct was properly initialized
    if (!data->num_rects || !data->image_width || !data->image_height ||
        !packer->rects || data->empty_areas.size != data->num_rects + 1) {
        return JAPACKER_ERROR_WRONG_PARAMETERS;
    }

    // Sort the rects if needed
    if (packer->options.rects_are_sorted != 1 || !data->sorted_rects) {
        if (!japacker_sort_rects(packer)) {
            return JAPACKER_ERROR_NO_MEMORY;
        }
    }

    // If we are forcing a full repack, we're effectively starting over, so we can't have existing images with rects
    if (packer->options.always_repack) {
        packer->result.images_needed = 0;
    }

    // Whether we need to pack the remaining rects to a new image
    int request_new_image;

    // The total number of rects we already packed
    unsigned int packed_rects = 0;

    // The area of the rectangles placed in this image
    unsigned int area_used_in_last_image;

    do {
        // When we're on a new image, the whole image is a single empty area
        japacker_reset_empty_areas(data, data->image_width, data->image_height);

        request_new_image = 0;
        area_used_in_last_image = 0;

        for (unsigned int i = 0; i < data->num_rects; i++) {
            japacker_rect *rect = data->sorted_rects[i];

            // We may have already processed some rects in previous images,
            // therefore we only pack the rects that haven't been packed yet
            if (rect->output.packed && (packer->options.always_repack != 1 || packer->result.images_needed != 0)) {
                continue;
            }

            // If we didn't skip a packed rect (likely because always_repack was set),
            // then we must unset that it's packed
            rect->output.packed = 0;

            // Pack the rectangle. If packing fails, what happens depends on what setting the user chose
            if (!japacker_pack_rect(data, rect, packer->options.allow_rotation)) {
                // We can try to continue packing smaller rectangles to this image
                if (packer->options.fail_policy == JAPACKER_CONTINUE) {
                    continue;
                // We can also keep packing, but pack the ones that didn't fit to a new image
                } else if (packer->options.fail_policy == JAPACKER_NEW_IMAGE) {
                    request_new_image = 1;
                // Or, by default, we can immediately stop packing
                } else {
                    return i;
                }

                // If this is a new image and the rectangle doesn't fit, it won't fit anywhere so we need to quit
                if (data->empty_areas.first->width == data->image_width &&
                    data->empty_areas.first->height == data->image_height) {
                    packer->result.images_needed--;
                    return packed_rects;
                }
            } else {
                rect->output.image_index = packer->result.images_needed;
                rect->output.packed = 1;
                area_used_in_last_image += rect->input.width * rect->input.height;
                packed_rects++;
            }
        }

        // We used a new image, so we increase its count
        packer->result.images_needed++;

    // If a new image was requested, we loop back to the top
    } while (request_new_image);

    // Set the last image's width and height to the size of the destination image as default
    // These values can be changed if japacker_reduce_last_image_size() is called
    packer->result.last_image_width = data->image_width;
    packer->result.last_image_height = data->image_height;

    // Try to reduce the last image's size if asked to
    if (packer->options.reduce_image_size == 1) {
        japacker_reduce_last_image_size(packer, area_used_in_last_image);
    }

    return packed_rects;
}

JAPACKER_DECL unsigned int japacker_get_dst_offset(const japacker_t *packer, const japacker_rect *rect,
    unsigned int x, unsigned int y)
{
    int dst_width;
    if (packer->options.reduce_image_size == 1 && rect->output.image_index == packer->result.images_needed - 1) {
        dst_width = packer->result.last_image_width;
    } else {
        dst_width = packer->internal_data->image_width;
    }
    if (rect->output.rotated == 0) {
        return (y + rect->output.y) * dst_width + rect->output.x + x;
    } else {
        return (rect->output.y + rect->input.width - 1) * dst_width + y + rect->output.x - x * dst_width;
    }
}

JAPACKER_DECL void japacker_free(japacker_t *packer)
{
    free(packer->internal_data->empty_areas.list);
    free(packer->internal_data->sorted_rects);
    free(packer->internal_data);
    packer->internal_data = 0;
}

#ifdef __cplusplus
}
#endif

#endif // JAPACKER_IMPORT

#endif // JAPACKER_H
