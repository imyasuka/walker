# The Walker Programming Language
Single pass recursive descent interpreted programming language buzzword buzzword buzzword you can have pointers to anything very metaprogramming!
# Example
```walker
{let;a;foo}
{let;foo bar;bazz}
{println;{{a} bar}}
```
This prints 'bazz'
This shows multiple things:
1. The basic lexical unit in this language is {function;argument 1;argument 2;argument 3}, almost like in LISP. 
2. You don't need quotation marks to use strings, that is because types are implicit and inferred based on function.
3. Variables can have spaces, and special characters too.
4. You can point to things directly using values from other things.
5. You can point to things indirectly using values from other things with additional values concatenated.
6. I said things there because you can point to basically anything.

What is left is to document what existing functions there are but for now you can study the source code to figure that out.
