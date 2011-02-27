# This file was automatically generated by SWIG (http://www.swig.org).
# Version 1.3.40
#
# Do not make changes to this file unless you know what you are doing--modify
# the SWIG interface file instead.

package Frozen;
use base qw(Exporter);
use base qw(DynaLoader);
package Frozenc;
bootstrap Frozen;
package Frozen;
@EXPORT = qw();

# ---------- BASE METHODS -------------

package Frozen;

sub TIEHASH {
    my ($classname,$obj) = @_;
    return bless $obj, $classname;
}

sub CLEAR { }

sub FIRSTKEY { }

sub NEXTKEY { }

sub FETCH {
    my ($self,$field) = @_;
    my $member_func = "swig_${field}_get";
    $self->$member_func();
}

sub STORE {
    my ($self,$field,$newval) = @_;
    my $member_func = "swig_${field}_set";
    $self->$member_func($newval);
}

sub this {
    my $ptr = shift;
    return tied(%$ptr);
}


# ------- FUNCTION WRAPPERS --------

package Frozen;

*frozen_init = *Frozenc::frozen_init;
*frozen_destroy = *Frozenc::frozen_destroy;
*backend_new = *Frozenc::backend_new;
*backend_bulk_new = *Frozenc::backend_bulk_new;
*backend_acquire = *Frozenc::backend_acquire;
*backend_find = *Frozenc::backend_find;
*backend_query = *Frozenc::backend_query;
*backend_destroy = *Frozenc::backend_destroy;
*configs_string_parse = *Frozenc::configs_string_parse;
*configs_file_parse = *Frozenc::configs_file_parse;
*hash_free = *Frozenc::hash_free;

# ------- VARIABLE STUBS --------

package Frozen;

*ACTION_CRWD_CREATE = *Frozenc::ACTION_CRWD_CREATE;
*ACTION_CRWD_READ = *Frozenc::ACTION_CRWD_READ;
*ACTION_CRWD_WRITE = *Frozenc::ACTION_CRWD_WRITE;
*ACTION_CRWD_DELETE = *Frozenc::ACTION_CRWD_DELETE;
*ACTION_CRWD_MOVE = *Frozenc::ACTION_CRWD_MOVE;
*ACTION_CRWD_COUNT = *Frozenc::ACTION_CRWD_COUNT;
*ACTION_CRWD_CUSTOM = *Frozenc::ACTION_CRWD_CUSTOM;
*REQUEST_INVALID = *Frozenc::REQUEST_INVALID;
1;
