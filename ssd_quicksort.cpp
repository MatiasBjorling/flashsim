/* Copyright 2009, 2010 Brendan Tauras */

/* ssd_quicksort.cpp is part of FlashSim. */

/* FlashSim is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version. */

/* FlashSim is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. */

/* You should have received a copy of the GNU General Public License
 * along with FlashSim.  If not, see <http://www.gnu.org/licenses/>. */

/****************************************************************************/

/* quicksort implementation for channel class
 * Brendan Tauras 2009-10-27
 *
 * Quicksort based on quicksort algorithm in 
 * Cormen, Leiserson, Rivest, and Stein: Introduction to Algorithms 2nd Ed.
 * on p146.
 *
 * Quicksort is used to sort the bus channel scheduling table in
 * ssd_channel.cpp .  The scheduling table is an array of doubles; this
 * implementation is a simple C implementation specifically for this sorting
 * task.  A templated C++ implementation could be used (with more overhead)
 * in the future if other types need to be supported.
 *
 * Quicksort was selected because this implementation is an in-place sort, and
 * the likelihood of only moving one entry when sorting lends towards an
 * insertion sort.  Quicksort can be viewed as an enhanced insertion sort.
 *
 * The algorithm is modified to perform the sort operations on 2 arrays
 * containing related data such that the sort is based on the first array, and
 * the same sorting operations are performed on the second array of related
 * data.  In the case of the 2-dimensional array in the channel class, the
 * C/C++ row-oriented array memory allocation is exploited.
 */

#include <stdlib.h>

#include "ssd.h"
namespace ssd{

static inline void swap(double *x, double *y)
{
	double tmp;
	tmp = *x;
	*x = *y;
	*y = tmp;
	return;
}

/* using (signed) long instead of unsigned int or size_t
 * because i can start at -1 */
long partition(double *array1, double *array2, long left, long right)
{
	long pivot = array1[right];
	long i = left - 1;
	long j;
	for(j = left; j < right; j++)
	{
		if(array1[j] <= pivot){
			swap(&array1[++i], &array1[j]);
			if(array2 != NULL)
				swap(&array2[i], &array2[j]);
		}
	}
	swap(&array1[++i], &array1[right]);
	if(array2 != NULL)
		swap(&array2[i], &array2[right]);
	return i;
}

/* call with base pointer of array and index of 1st and last element in
 * inclusive array element range to sort 
 *
 * both arrays should be same size, or 2nd array can be NULL */
void quicksort(double *array1, double *array2, long left, long right)
{
	long split;
	if(left < right)
	{
		split = partition(array1, array2, left, right);
		quicksort(array1, array2, left, split - 1);
		quicksort(array1, array2, split + 1, right);
	}
	return;
}

}

/* proof of concept to make sure the sort works for the channel class */
#if 0
#include <stdio.h>
int main(void){
	long i;
/* 	double ary[2][8] = {{2,8,7,1,3,5,6,4}, {2,8,7,1,3,5,6,4}}; */
/* 	double ary[8][2] = {{2,2},{8,8},{4,4},{1,1},{5,5},{3,3},{6,6},{7,7}}; */
/* 	double *a = &ary[0][0]; */
/* 	double *b = &ary[1][0]; */
/* 	double *b = NULL; */
	double a[8] = {2,8,7,1,3,5,6,4};
	double b[8] = {2,8,7,1,3,5,6,4};

	for(i = 0; i < 8; i++)
	{
		if(b == NULL)
			printf("%lf, ", a[i]);
		else
			printf("(%lf,%lf) ", a[i], b[i]);
	}
	printf("\n\n");

	ssd::quicksort(a, b, 0, 7);

	for(i = 0; i < 8; i++)
	{
		if(b != NULL)
			printf("(%lf,%lf) ", a[i], b[i]);
		else
			printf("%lf, ", a[i]);
	}
	printf("\n");
	return 0;
}
#endif
