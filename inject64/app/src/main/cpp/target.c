/*
 * target.c
 *
 *  Created on: 2015年6月26日
 *      Author: Administrator
 */

#include<stdio.h>

int main(int argc,char **argv)
{
	static unsigned int i =0;
	while(1){
		sleep(1000);
		printf("i am target64 program %d",i++);
	}
	return 0;
}
