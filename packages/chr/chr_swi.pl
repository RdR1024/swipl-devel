/*  $Id$

    Part of CHR (Constraint Handling Rules)

    Author:        Tom Schrijvers
    E-mail:        Tom.Schrijvers@cs.kuleuven.ac.be
    WWW:           http://www.swi-prolog.org
    Copyright (C): 2003-2004, K.U. Leuven

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    As a special exception, if you link this library with other files,
    compiled with a Free Software compiler, to produce an executable, this
    library does not by itself cause the resulting executable to be covered
    by the GNU General Public License. This exception does not however
    invalidate any other reasons why the executable file might be covered by
    the GNU General Public License.
*/

:- module(chr,
	  [ chr_compile/2,		% +CHRFile, -PlFile
	    chr_show_store/1,		% +Module
	    chr_trace/0,
	    chr_notrace/0
	  ]).
:- use_module(library(listing)).	% portray_clause/2
:- use_module(library('chr/chr_translate')).
:- use_module(library('chr/chr_debug')).
:- use_module(library('chr/chr_runtime')).
:- include(library('chr/chr_op')).

		 /*******************************
		 *	     LOAD HOOK		*
		 *******************************/

:- multifile
	user:prolog_load_file/2,
	user:prolog_file_type/2.
:- dynamic
	user:prolog_load_file/2,
	user:prolog_file_type/2.

%	user:prolog_file_type(Extension, FileType).

user:prolog_file_type(chr, chr).


%	prolog_load_file(+Spec, +Options)
%	
%	Load files ending with the .chr extension by compiling them to
%	Prolog and loading the resulting Prolog file.
%	
%	TBD: deal properly with make/0.

user:prolog_load_file(Spec, Options) :-
	'$strip_module'(Spec, Module, File),
	absolute_file_name(File,
			   [ access(read),
			     file_errors(fail),
			     file_type(chr)
			   ],
			   CHRFile),
	file_name_extension(Base, chr, CHRFile), !,
	file_name_extension(Base, pl, PlFile),
	ensure_chr_compiled(CHRFile, PlFile, Options),
	load_files(Module:PlFile,
		   [ derived_from(CHRFile)
		   | Options
		   ]).

ensure_chr_compiled(CHRFile, PlFile, _) :-
	time_file(CHRFile, CHRTime),
	time_file(PlFile, PlTime),
	PlTime > CHRTime, !.
ensure_chr_compiled(CHRFile, PlFile, Options) :-
	(   memberchk(silent(true), Options)
	->  MsgLevel = silent
	;   MsgLevel = informational
	),
	chr_compile(CHRFile, PlFile, MsgLevel).


		 /*******************************
		 *    FILE-TO-FILE COMPILER	*
		 *******************************/

%	chr_compile(+CHRFile, -PlFile)
%	
%	Compile a CHR specification into a Prolog file.  The double
%	\+ \+ is done to reclaim all changes to the global constraint
%	pool.  This should be done by chr_translate/2 (JW).

chr_compile(From, To) :-
	chr_compile(From, To, informational).

chr_compile(From, To, MsgLevel) :-
	\+ \+ chr_compile2(From, To, MsgLevel), !.
chr_compile(From, _, _) :-
	print_message(error, chr(compilation_failed(From))),
	fail.

chr_compile2(From, To, MsgLevel) :-
	print_message(MsgLevel, chr(start(From))),
	read_chr_file_to_terms(From, Declarations),
	% read_file_to_terms(From, Declarations,
	% 		   [ module(chr) 	% get operators from here
	%		   ]),
	print_message(silent, chr(translate(From))),
	chr_translate(Declarations, Declarations1),
	insert_declarations(Declarations1, NewDeclarations),
	print_message(silent, chr(write(To))),
	writefile(To, From, NewDeclarations),
	print_message(MsgLevel, chr(end(From, To))).


insert_declarations(Clauses0, Clauses) :-
	(   Clauses0 = [:- module(M,E)|FileBody]
	->  Clauses = [ :- module(M,E),
			:- use_module(library('chr/chr_runtime')),
			:- style_check(-singleton),
			:- style_check(-discontiguous)
		      | FileBody
		      ]
	;   Clauses = [ :- use_module(library('chr/chr_runtime')),
			:- style_check(-singleton),
			:- style_check(-discontiguous)
		      | Clauses0
		      ]
	).

%	writefile(+File, +From, +Desclarations)
%	
%	Write translated CHR declarations to a File.

writefile(File, From, Declarations) :-
	open(File, write, Out),
	writeheader(From, Out),
	writecontent(Declarations, Out),
	close(Out).

writecontent([], _).
writecontent([D|Ds], Out) :-
	portray_clause(Out, D),		% SWI-Prolog
	writecontent(Ds, Out).


writeheader(File, Out) :-
	get_time(Now),
	convert_time(Now, Date),
	format(Out, '/*  Generated by CHR compiler~n', []),
	format(Out, '    From: ~w~n', [File]),
	format(Out, '    Date: ~w~n~n', [Date]),
	format(Out, '    DO NOT EDIT.  EDIT THE CHR FILE INSTEAD~n', []),
	format(Out, '*/~n~n', []).


		 /*******************************
		 *	       MESSAGES		*
		 *******************************/


:- multifile
	prolog:message/3.

prolog:message(chr(start(File))) -->
	{ file_base_name(File, Base)
	},
	[ 'Translating CHR file ~w'-[Base] ].
prolog:message(chr(end(_From, To))) -->
	{ file_base_name(To, Base)
	},
	[ 'Written translation to ~w'-[Base] ].
prolog:message(chr(compilation_failed(From))) -->
	[ 'CHR: Failed to compile ~w'-[From] ].


%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
read_chr_file_to_terms(Spec, Terms) :-
	absolute_file_name(Spec, [ access(read) ],
			   Path),
	open(Path, read, Fd, []),
	read_chr_stream_to_terms(Fd, Terms),
	close(Fd).

read_chr_stream_to_terms(Fd, Terms) :-
	read_term(Fd, C0, [ module(chr) ]),
	read_chr_stream_to_terms(C0, Fd, Terms).

read_chr_stream_to_terms(end_of_file, _, []) :- !.
read_chr_stream_to_terms(C, Fd, [C|T]) :-
	( ground(C),
	  C = (:- op(Priority,Type,Name)) ->
		op(Priority,Type,Name)
	;
		true
	),
	read_term(Fd, C2, [module(chr)]),
	read_chr_stream_to_terms(C2, Fd, T).
