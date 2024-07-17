// // ringbuffer
// typedef struct 
// {    
//     u8							  bMax;                 // 环形缓冲区的最大索引    
//     u8							  bWrIx; 				// 写索引    
//     u8							  bRdIx;                // 读索引    
// }Cl2_Packet_Fifo_Type;
// Cl2_Packet_Fifo_Type *ringbuffer;

// 	/*****************************************************************************
// 	 Prototype    : Cl2FifoCreateFifo
// 	Description  : 
// 	Input        : 
// 	Output       : None
// 	Return Value : 
// 	Calls        : 
// 	Called By    : 
	
// 	History        :
// 	1.Date         : 2024/6/13
// 		Author       : 
// 		Modification : Created function

// 	*****************************************************************************/
// 	Cl2_Packet_Fifo_Type *Cl2FifoCreateFifo(u8 bDepth) 
// 	{    
// 		Cl2_Packet_Fifo_Type *psFifo = NULL;    
// 		u16					  wNewDepth;    
// 		u16					  wBufRequired;    

// 		/* Choose Depth */    
// 		wNewDepth = 1;    

// 		while (wNewDepth < bDepth)    
// 		{        
// 			wNewDepth <<= 1;    
// 		}    

// 		wBufRequired = sizeof(Cl2_Packet_Fifo_Type) + wNewDepth * sizeof(struct scatterlist *);    
// 		psFifo = (Cl2_Packet_Fifo_Type *)kmalloc(wBufRequired, GFP_KERNEL);   

// 		if (psFifo == NULL)    
// 		{        
// 			return NULL;    
// 		}    

// 		psFifo->bMax 	= wNewDepth - 1;    
// 		psFifo->bWrIx 	= 0;    
// 		psFifo->bRdIx 	= 0;    
		    
// 		return psFifo;
// 	}

// 	/*****************************************************************************
// 	 Prototype    : Cl2FifoRemoveFifo
// 	Description  : 
// 	Input        : 
// 	Output       : None
// 	Return Value : 
// 	Calls        : 
// 	Called By    : 
	
// 	History        :
// 	1.Date         : 2024/6/13
// 		Author       : 
// 		Modification : Created function

// 	*****************************************************************************/
// 	void Cl2FifoRemoveFifo(Cl2_Packet_Fifo_Type *psFifo)
// 	{    
// 		if (psFifo == NULL)    
// 		{        
// 			return;    
// 		}    

// 		kfree(psFifo);
// 		printk(KERN_ERR "remove ringbuffer\n");
// 	}


// //notify begin
	
	
// //notify end